/**********************************************************
 * cvs_ext.c
 *
 * Copyright 2004, Stefan Siegl <ssiegl@gmx.de>, Germany
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Publice License,
 * version 2 or any later. The license is contained in the COPYING
 * file that comes with the cvsfs4hurd distribution.
 *
 * connect to cvs :ext: server
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <malloc.h>
#include <sys/types.h>
#include <signal.h>

#include "cvsfs.h"
#include "cvs_ext.h"
#include "cvs_connect.h"



/* cvs_ext_connect
 *
 * connect to the cvs :ext: server as further described in the cvsfs_config
 * configuration structure
 */
error_t
cvs_ext_connect(FILE **send, FILE **recv)
{
  char port[10];
  int fd_to_rsh[2], fd_from_rsh[2];
  pid_t pid;

  if(pipe(fd_to_rsh))
    return errno;
  if(pipe(fd_from_rsh))
    return errno;

  if((pid = fork()) < 0)
    {
      perror(PACKAGE ": cannot fork remote shell client");
      return pid;
    }

  if(! pid)
    {
      /* okay, child process, fork to remote shell client */
      close(fd_to_rsh[1]);   /* close writing end */
      close(fd_from_rsh[0]); /* close reading end */

      if(dup2(fd_to_rsh[0], 0) < 0 || dup2(fd_from_rsh[1], 1) < 0)
	{
	  perror(PACKAGE ": unable to dup2 pipe to stdin/stdout");
	  exit(1);
	}

      snprintf(port, sizeof(port), "%d",
	       config.cvs_port ? config.cvs_port : 22);

      execlp(config.cvs_shell_client, config.cvs_shell_client,
	     "-p", port,
	     "-l", config.cvs_username, config.cvs_hostname,
	     "--", "cvs", "server", NULL);
      exit(1);
    }

  close(fd_to_rsh[0]);
  close(fd_from_rsh[1]);

  if(! ((*send = fdopen(fd_to_rsh[1], "w"))
	&& (*recv = fdopen(fd_from_rsh[0], "r"))))
    {
      perror(PACKAGE ": unable to convert pipe's fds to file streams");

      if(send)
	fclose(*send);
      else
	close(fd_to_rsh[1]);

      if(recv)
	fclose(*recv);
      else
	close(fd_from_rsh[0]);

      kill(pid, SIGTERM);
      return EIO;
    }

  if(setvbuf(*send, NULL, _IOLBF, 0) || setvbuf(*recv, NULL, _IOLBF, 0))
    {
      perror(PACKAGE ": cannot force streams to be linebuffered");
      fclose(*send);
      fclose(*recv);
      return EIO;
    }

  return 0;
}
