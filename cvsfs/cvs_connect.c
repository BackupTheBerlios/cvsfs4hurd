/**********************************************************
 * cvs_connect.c
 *
 * Copyright (C) 2004, 2005 by Stefan Siegl <ssiegl@gmx.de>, Germany
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

/* callback function for SIGPIPE signal (i.e. our remote client died) */
static void cvs_connect_sigpipe_handler(int);

/* cvs_connect_init
 *
 * initialize cvs_connect stuff
 */
void
cvs_connect_init(void)
{
  FUNC_PROLOGUE("cvs_connect_init");

  /* first things first: initialize global locks we use */
  spin_lock_init(&cvs_cached_conn_lock);

  signal(SIGALRM, cvs_connect_sigalrm_handler);
  alarm(30);

  signal(SIGUSR1, cvs_connect_sigusr1_handler);
  signal(SIGUSR2, cvs_connect_sigusr2_handler);
  signal(SIGPIPE, cvs_connect_sigpipe_handler);

  FUNC_EPILOGUE_NORET();
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
  FUNC_PROLOGUE("cvs_connect");

  /* look whether we've got a cached connection available */
  spin_lock(&cvs_cached_conn_lock);

  if((*send = cvs_cached_conn.send) && (*recv = cvs_cached_conn.recv))
    {
      cvs_cached_conn.send = NULL;
      cvs_cached_conn.recv = NULL;

      /* do a quick still-alive check, in order to avoid confusion of the
       * calling routine 
       */
      fprintf(*send, "noop\n");

      if(! cvs_wait_ok(*recv))
	{
	  /* okay, connection still alive */
	  spin_unlock(&cvs_cached_conn_lock);
	  FUNC_RETURN(0);
	}
      else
	DEBUG("cvs-connect", "cached connection not alive anymore");
    }

  spin_unlock(&cvs_cached_conn_lock);

  /* get a fresh, new connection ... */
  FUNC_EPILOGUE(cvs_connect_fresh(send, recv));
}



/* cvs_connect_fresh
 *
 * Try connecting to the cvs host, like cvs_connect, but get get
 * a fresh connection ...
 */
error_t
cvs_connect_fresh(FILE **send, FILE **recv)
{
  error_t err = 0;

  switch(config.cvs_mode)
    {
    case PSERVER:
      err = cvs_pserver_connect(send, recv);
      break;

    case EXT:
    case LOCAL:
      err = cvs_ext_connect(send, recv);
      break;
    }

  if(err)
    return err; /* something went wrong, we already logged, what did */

  /* still looks good. inform server of our cvs root */
  fprintf(*send, "Root %s\n", config.cvs_root);

  if(cvs_handshake(*send, *recv))
    {
      cvs_connection_kill(*send, *recv);
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
    /* there's already a cached connection, forget about ours */
    cvs_connection_kill(send, recv);

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
	  "Mod-time " 
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

#if HAVE_LIBZ
      /* check whether the server supports gzip compression */
      if(! strstr(buf, "gzip-file-contents"))
	config.gzip_level = 0; /* not supported. */
#endif
    }

#if HAVE_LIBZ
  /* wait for the 'ok' message of valid-requests ... */
  if(cvs_wait_ok(recv))
    return EIO;

  if(config.gzip_level)
    /* request gzip compression ... */
    fprintf(send, "gzip-file-contents %d\n", config.gzip_level);

  /* 'ok' of valid-requests already read, therefore simply return. */
  return 0;

#else /* ! HAVE_LIBZ */
  return cvs_wait_ok(recv);
#endif
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
      if(cvs_cached_conn.send != cvs_cached_conn.recv)
	fclose(cvs_cached_conn.recv);

      cvs_cached_conn.send = NULL;
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
      if(cvs_cached_conn.send != cvs_cached_conn.recv)
	fclose(cvs_cached_conn.recv);

      cvs_cached_conn.send = NULL;
      cvs_cached_conn.recv = NULL;
    }

  spin_unlock(&cvs_cached_conn_lock);
}



/* callback function for SIGPIPE signal (i.e. our remote client died) */
static void
cvs_connect_sigpipe_handler(int sig) 
{
  FUNC_PROLOGUE_FMT("cvs_connect_sigpipe_hanler", "signal=%d", sig);

  /* just ignore the SIGPIPE signal, which we'll receive, if we either read 
   * from or write to the pipe to one of our subprocesses (remote shell
   * clients), in case these are not alive anymore.
   *
   * this especially happens if we try to reuse a cache connection,
   * where the remote side has an idle daemon, etc.
   */

  FUNC_EPILOGUE_NORET();
}
