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

#define PACKAGE "cvsfs"

/* do cvs handshake, aka tell about valid responses and check whether all
 * necessary requests are supported.
 */
static int cvs_handshake(FILE *cvs_handle);

/* try to keep one connection to the cvs host open, FILE* handle of our
 * connection + rwlock, which must be held, when modifying 
 */
static FILE *cvs_cached_conn = NULL;
spin_lock_t cvs_cached_conn_lock;
time_t cvs_cached_conn_release_time = 0;

/* callback function we install for SIGALRM signal */
static void cvs_connect_sigalrm_handler(int);

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
}

/* cvs_connect
 *
 * Try connecting to the cvs host specified in 'config'. Return a
 * line-buffer libc FILE* handle on success, NULL if something failed.
 */
FILE *
cvs_connect(cvsfs_config *config)
{
  FILE *cvs_handle = NULL;
  char buf[128]; /* we only need to read something like I LOVE YOU
		  * or some kind of error message (E,M)
		  */

  /* look whether we've got a cached connection available */
  spin_lock(&cvs_cached_conn_lock);
  if((cvs_handle = cvs_cached_conn))
    cvs_cached_conn = NULL;
  spin_unlock(&cvs_cached_conn_lock);

  if(cvs_handle)
    return cvs_handle; /* cached connection was available */

  switch(config->cvs_mode)
    {
    case PSERVER:
      cvs_handle = cvs_pserver_connect(config);
      break;
    }

  if(! cvs_handle)
    return NULL; /* something went wrong, we already logged, what did */

  /* okay, now watch out for the server's answer,
   * in the hope, that it loves us
   */
  if(! fgets(buf, sizeof(buf), cvs_handle))
    {
      perror(PACKAGE);
      fclose(cvs_handle);
      return NULL;
    }

  if(strncmp(buf, "I LOVE YOU", 10))
    {
      cvs_treat_error(cvs_handle, buf);
      fclose(cvs_handle);
      return NULL;
    }

  /* still looks good. inform server of our cvs root */
  fprintf(cvs_handle, "Root %s\n", config->cvs_root);

  if(cvs_handshake(cvs_handle))
    {
      fclose(cvs_handle);
      return NULL;
    }

  return cvs_handle;
}


/* cvs_connection_release
 *
 * release the connection cvs_handle.  the connection may then either be cached
 * and reused on next cvs_connect() or may be closed.
 */
void
cvs_connection_release(FILE *cvs_handle)
{
  spin_lock(&cvs_cached_conn_lock);

  if(cvs_cached_conn)
    /* there's already a cached connection, forget about ours */
    fclose(cvs_handle);

  else
    {
      cvs_cached_conn = cvs_handle;
      cvs_cached_conn_release_time = time(NULL);
    }

  spin_unlock(&cvs_cached_conn_lock);
}


/* cvs_handshake
 *
 * do cvs handshake, aka tell about valid responses and check whether all
 * necessary requests are supported.
 */
static int
cvs_handshake(FILE *cvs_handle)
{
  char buf[4096]; /* Valid-requests answer can be really long ... */

  fprintf(cvs_handle, "Valid-responses "
	  /* base set of responses, we need to understand those ... */
	  "ok error Valid-requests M E "

	  /* cvs needs Checked-in Updated Merged and Removed, else it just
	   * dies, complaining. However we'll never need to understand those,
	   * as long as our filesystem stays readonly.
	   */
	  "Checked-in Updated Merged Removed"
	  "\n");
  fprintf(cvs_handle, "valid-requests\n");

  if(! fgets(buf, sizeof(buf), cvs_handle))
    {
      perror(PACKAGE);
      return 1; /* connection gets closed by caller! */
    }

  /* TODO: care for the Valid-requests response, make sure everything we
   * need is there
   */

  if(strncmp(buf, "Valid-requests ", 15))
    {
      cvs_treat_error(cvs_handle, buf);
      return 1; /* connection will be closed by our caller */
    }

  return cvs_wait_ok(cvs_handle);
}



/* cvs_wait_ok
 * 
 * read one line from cvs server and make sure, it's an ok message. else
 * call cvs_treat_error. return 0 on 'ok'.
 */
int
cvs_wait_ok(FILE *cvs_handle) 
{
  char buf[128];

  if(fgets(buf, sizeof(buf), cvs_handle))
    {
      if(! strncmp(buf, "ok", 2))
	return 0;

      cvs_treat_error(cvs_handle, buf);
      return 1;
    }

  return 1; /* hmm, didn't work, got eof */
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
  spin_lock(&cvs_cached_conn_lock);
  if(cvs_cached_conn
     && (time(NULL) - cvs_cached_conn_release_time > 90))
    {
      /* okay, connection is rather old, drop it ... */
      fclose(cvs_cached_conn);
      cvs_cached_conn = NULL;
    }
  spin_unlock(&cvs_cached_conn_lock);
}
