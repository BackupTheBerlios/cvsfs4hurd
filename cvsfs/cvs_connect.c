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

#include "cvsfs.h"
#include "cvs_connect.h"
#include "cvs_pserver.h"

#define PACKAGE "cvsfs"

static int cvs_handshake(FILE *cvs_handle);

/* cvs_connect
 *
 * Try connecting to the cvs host specified in 'config'. Return a
 * line-buffer libc FILE* handle on success, NULL if something failed.
 */
FILE *
cvs_connect(cvsfs_config *config)
{
  FILE *cvs_handle;
  char buf[128]; /* we only need to read something like I LOVE YOU
		  * or some kind of error message (E,M)
		  */
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
