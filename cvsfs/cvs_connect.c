/**********************************************************
 * cvs_connect.c
 *
 * Copyright 2004, Stefan Siegl <ssiegl@gmx.de>, Germany
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Publice License,
 * version 2 or any later. The license is contained in the COPYING
 * file that comes with the cvsfs4hurd distribution.
 *
 * connect to cvs server
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <spin-lock.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include "cvsfs.h"
#include "cvs_connect.h"
#include "cvs_pserver.h"
#include "cvs_ext.h"
#include "cvs_tree.h"

/* do cvs handshake, aka tell about valid responses and check whether all
 * necessary requests are supported.
 */
static error_t cvs_handshake(FILE *send, FILE *recv);

/* try to keep one connection to the cvs host open, FILE* handle of our
 * connection + rwlock, which must be held, when modifying 
 */
static struct {
  FILE *send;
  FILE *recv;
} cvs_cached_conn = { NULL, NULL };
spin_lock_t cvs_cached_conn_lock;
time_t cvs_cached_conn_release_time = 0;

/* callback function we install for SIGALRM signal */
static void cvs_connect_sigalrm_handler(int);

/* callback function we install for SIGUSR1 to force cvstree update */
static void cvs_connect_sigusr1_handler(int);

/* callback function for SIGUSR2, to force disconnection of cached conn. */
static void cvs_connect_sigusr2_handler(int);

/* cvs_connect_init
 *
 * initialize cvs_connect stuff
 */
void
cvs_connect_init(void)
{
  /* first things first: initialize global locks we use */
  spin_lock_init(&cvs_cached_conn_lock);

  signal(SIGALRM, cvs_connect_sigalrm_handler);
  alarm(30);

  signal(SIGUSR1, cvs_connect_sigusr1_handler);
  signal(SIGUSR2, cvs_connect_sigusr2_handler);
}

/* cvs_connect
 *
 * Try connecting to the cvs host. Return 0 on success. 
 * FILE* handles to send and receive are guaranteed to be valid only, if zero
 * status was returned. The handles are line-buffered.
 */
error_t
cvs_connect(FILE **send, FILE **recv)
{
  error_t err = 0;

  /* look whether we've got a cached connection available */
  spin_lock(&cvs_cached_conn_lock);

  if((*send = cvs_cached_conn.send) && (*recv = cvs_cached_conn.recv))
    {
      cvs_cached_conn.send = NULL;
      cvs_cached_conn.recv = NULL;

      spin_unlock(&cvs_cached_conn_lock);
      return 0;
    }

  spin_unlock(&cvs_cached_conn_lock);

  switch(config.cvs_mode)
    {
    case PSERVER:
      err = cvs_pserver_connect(send, recv);
      break;

    case EXT:
      err = cvs_ext_connect(send, recv);
      break;
    }

  if(err)
    return err; /* something went wrong, we already logged, what did */

  /* still looks good. inform server of our cvs root */
  fprintf(*send, "Root %s\n", config.cvs_root);

  if(cvs_handshake(*send, *recv))
    {
      fclose(*send);
      fclose(*recv);
      return EIO;
    }

  return 0;
}


/* cvs_connection_release
 *
 * release the connection.  the connection may then either be cached
 * and reused on next cvs_connect() or may be closed.
 */
void
cvs_connection_release(FILE *send, FILE *recv)
{
  spin_lock(&cvs_cached_conn_lock);

  if(cvs_cached_conn.send)
    {
      /* there's already a cached connection, forget about ours */
      fclose(send);
      fclose(recv);
    }
  else
    {
      cvs_cached_conn.send = send;
      cvs_cached_conn.recv = recv;

      cvs_cached_conn_release_time = time(NULL);
    }

  spin_unlock(&cvs_cached_conn_lock);
}


/* cvs_handshake
 *
 * do cvs handshake, aka tell about valid responses and check whether all
 * necessary requests are supported.
 */
static error_t
cvs_handshake(FILE *send, FILE *recv)
{
  char buf[4096]; /* Valid-requests answer can be really long ... */

  fprintf(send, "Valid-responses "
	  /* base set of responses, we need to understand those ... */
	  "ok error Valid-requests M E "

	  /* cvs needs Checked-in Updated Merged and Removed, else it just
	   * dies, complaining. However we'll never need to understand those,
	   * as long as our filesystem stays readonly.
	   */
	  "Checked-in Updated Merged Removed"
	  "\n");
  fprintf(send, "valid-requests\n");

  if(! fgets(buf, sizeof(buf), recv))
    {
      perror(PACKAGE);
      return 1; /* connection gets closed by caller! */
    }

  if(strncmp(buf, "Valid-requests ", 15))
    {
      cvs_treat_error(recv, buf);
      return 1; /* connection will be closed by our caller */
    }
  else
    {
      const char **reqs_ptr;
      const char *cvs_needed_reqs[] = {
	"valid-requests", "Root", "Valid-responses", "UseUnchanged",
	"Argument", "rdiff", 

	/* terminate list */
	NULL
      };

      for(reqs_ptr = cvs_needed_reqs; *reqs_ptr; reqs_ptr ++)
	if(! strstr(buf, *reqs_ptr))
	  {
	    fprintf(stderr, PACKAGE ": cvs server doesn't understand "
		    "'%s' command, cannot live with that.\n", *reqs_ptr);

	    /* tell caller to close connection ... */
	    return EIO;
	  }
    }

  return cvs_wait_ok(recv);
}



/* cvs_wait_ok
 * 
 * read one line from cvs server and make sure, it's an ok message. else
 * call cvs_treat_error. return 0 on 'ok'.
 */
error_t
cvs_wait_ok(FILE *cvs_handle) 
{
  char buf[128];

  if(fgets(buf, sizeof(buf), cvs_handle))
    {
      if(! strncmp(buf, "ok", 2))
	return 0;

      cvs_treat_error(cvs_handle, buf);
      return EIO;
    }

  return EIO; /* hmm, didn't work, got eof */
}



/* cvs_treat_error
 * 
 * notify the user (aka log to somewhere) that we've received some error
 * messages, we don't know how to handle ...
 */
void
cvs_treat_error(FILE *cvs_handle, char *msg) 
{
  char buf[128];

  do
    if(msg)
      {
	/* chop trailing linefeed off of msg */
	char *ptr = msg + strlen(msg) - 1;
	if(*ptr == 10) *ptr = 0;

	if(msg[1] == ' ' && (*msg == 'M' || *msg == 'E'))
	  fprintf(stderr, PACKAGE ": %s\n", &msg[2]);

	else if(! strncmp(msg, "error ", 6))
	  {
	    for(msg += 6; *msg && *msg <= '9'; msg ++);
	    if(*msg)
	      fprintf(stderr, PACKAGE ": error: %s\n", msg);
	    return;
	  }

	else
	  fprintf(stderr, PACKAGE ": protocol error, received: %s\n", msg);
      }
  while((msg = fgets(buf, sizeof(buf), cvs_handle)));

  /* fprintf(stderr, "leaving treat_error, due to received eof\n"); */
}



/* cvs_connect_sigalrm_handler
 * 
 * callback function we install for SIGALRM signal
 *   -> shutdown cvs server connection if idle for more than 90 seconds
 */
static void 
cvs_connect_sigalrm_handler(int signal) 
{
  static time_t cvs_tree_expiration = 0;

  /* update directory tree, by default every 1800 sec. */
  if(! cvs_tree_expiration)
    time(&cvs_tree_expiration);

  if(time(NULL) - cvs_tree_expiration > 1800)
    cvs_tree_read(&rootdir);

  /* expire rather old cached connections ... */
  spin_lock(&cvs_cached_conn_lock);

  if(cvs_cached_conn.send
     && (time(NULL) - cvs_cached_conn_release_time > 90))
    {
      /* okay, connection is rather old, drop it ... */
      fclose(cvs_cached_conn.send);
      cvs_cached_conn.send = NULL;

      fclose(cvs_cached_conn.recv);
      cvs_cached_conn.recv = NULL;
    }

  spin_unlock(&cvs_cached_conn_lock);
}


/* cvs_connect_sigusr1_handler
 *
 * callback function we install for SIGUSR1 to force cvstree update
 */
static void
cvs_connect_sigusr1_handler(int sig) 
{
  cvs_tree_read(&rootdir);
}


/* cvs_connect_sigusr2_handler
 *
 * callback function for SIGUSR2, to force disconnection of cached conn.
 */
static void
cvs_connect_sigusr2_handler(int sig)
{
  spin_lock(&cvs_cached_conn_lock);

  if(cvs_cached_conn.send)
    {
      fclose(cvs_cached_conn.send);
      cvs_cached_conn.send = NULL;

      fclose(cvs_cached_conn.recv);
      cvs_cached_conn.recv = NULL;
    }

  spin_unlock(&cvs_cached_conn_lock);
}
