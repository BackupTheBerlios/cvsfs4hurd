/**********************************************************
 * cvs_pserver.c
 *
 * Copyright 2004, Stefan Siegl <ssiegl@gmx.de>, Germany
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Publice License,
 * version 2 or any later. The license is contained in the COPYING
 * file that comes with the cvsfs4hurd distribution.
 *
 * talk pserver protocol
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#define PACKAGE "cvsfs"

#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <stdlib.h>

#include "cvsfs.h"
#include "cvs_pserver.h"
#include "tcpip.h"

/* look for a password entry in $HOME/.cvspass file, permitting login
 * with credentials from given config structure.
 */
static char *cvs_pserver_fetch_pw(cvsfs_config *config);



/* cvs_pserver_connect
 *
 * connect to the cvs pserver as further described in the cvsfs_config
 * configuration structure
 */
FILE *
cvs_pserver_connect(cvsfs_config *config)
{
  FILE *cvs_handle = tcpip_connect(config->cvs_hostname, config->cvs_port);

  if(! cvs_handle) 
    /* tcpip connection couldn't be brought up, tcpip_connect spit out a 
     * logmessage itself ...
     */ 
    return NULL; 

  if(! config->cvs_password)
    config->cvs_password = cvs_pserver_fetch_pw(config);

  /* okay, now let's talk a little pserver dialect to log in ... */
  fprintf(cvs_handle, "BEGIN AUTH REQUEST\n");
  fprintf(cvs_handle, "%s\n%s\n%s\n", 
	  config->cvs_root,
	  config->cvs_username,
	  config->cvs_password);
  fprintf(cvs_handle, "END AUTH REQUEST\n");

  /* the result of our login request is handled in cvs_connect()
   * since this is equal to all supported protocols
   */
  return cvs_handle;
}



/* cvs_pserver_fetch_pw
 * 
 * look for a password entry in $HOME/.cvspass file, permitting login
 * with credentials from given config structure.
 * make sure to free() the returned memory, if needed!
 */
static char *
cvs_pserver_fetch_pw(cvsfs_config *config)
{
  char buf[512]; /* 512 byte should be enough for most CVSROOTs, if
		  * cvsfs tell's you to increase this value, please do so.
		  */
  char *cvspass_path;
  FILE *cvspass;
  char *cvsroot;
  int cvsroot_len;
  const char null_pw[] = "A"; /* empty password, returned if we fail */

  if(! config->homedir)
    config->homedir = getenv("HOME");
  
  if(! config->homedir)
    {
      /* hmm, HOME environment variable not set, try scaning /etc/passwd
       * for the homedir path ...
       */
      uid_t uid = getuid();
      struct passwd *pwent;

      for(pwent = getpwent(); pwent; getpwent())
	if(pwent->pw_uid == uid)
	  {
	    config->homedir = strdup(pwent->pw_dir);
	    break;
	  }

      endpwent();
    }
  
  if(! config->homedir)
    {
      fprintf(stderr, PACKAGE ": cannot figure out what your homedir is. "
	      "trying empty password.\n");
      return strdup(null_pw);
    }

  if(! (cvspass_path = malloc(strlen(config->homedir) + 10)))
    {
      perror(PACKAGE);
      return strdup(null_pw); /* I pray for it to have a long lasting life! */
    }

  sprintf(cvspass_path, "%s/.cvspass", config->homedir);

  if(! (cvspass = fopen(cvspass_path, "r")))
    {
      perror(PACKAGE ": cannot open .cvspass file for reading");
      free(cvspass_path);
      fprintf(stderr, PACKAGE ": trying to log in without password.\n");
      return strdup(null_pw);
    }

  free(cvspass_path);
  
  /* predict length of cvsroot string */
  cvsroot_len = 20 + strlen(config->cvs_username) +
    strlen(config->cvs_hostname) + strlen(config->cvs_root);

  if(! (cvsroot = malloc(cvsroot_len)))
    {
      fclose(cvspass);
      perror(PACKAGE);
      return strdup(null_pw); /* I pray for it to have a long lasting life! */
    }

  cvsroot_len = snprintf(cvsroot, cvsroot_len, ":pserver:%s@%s:%d%s",
			 config->cvs_username, config->cvs_hostname,
			 config->cvs_port, config->cvs_root);

  while(fgets(buf, sizeof(buf), cvspass))
    {
      char *ptr = buf + strlen(buf);
      ptr --;

      if(*ptr != 10)
	{
	  fprintf(stderr, PACKAGE "cvs_pserver_fetch_pw's parse buffer is "
		  "too small, stop for the moment.\n");
	  exit(10);
	}

      /* chop the linefeed off the end */
      *ptr = 0;

      if(buf[0] != '/' || buf[1] != '1' || buf[2] != ' ')
	continue; /* syntax error, well, ignore silently ... */

      ptr = buf + 3;

      if(strncmp(ptr, cvsroot, cvsroot_len))
	continue; /* didn't match, try next one ... */

      ptr += cvsroot_len;
      if(*(ptr ++) != ' ')
	continue; /* missing separator, cvsroot of .cvspass differs ... */

      /* okay, ptr points to where the password begins ... */
      fclose(cvspass);
      free(cvsroot);

      return strdup(ptr);
    }

  /* hmm, eof reached, but no password found! */
  fprintf(stderr, PACKAGE ": cannot find password for CVSROOT '%s' in "
	  "your .cvspass file, trying no password at all\n", cvsroot);

  fclose(cvspass);
  free(cvsroot);

  return strdup(null_pw);
}

