/**********************************************************
 * tcpip.c
 *
 * Copyright 2004, Stefan Siegl <ssiegl@gmx.de>, Germany
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Publice License,
 * version 2 or any later. The license is contained in the COPYING
 * file that comes with the cvsfs4hurd distribution.
 *
 * speak tcp/ip protocol, aka connect to tcp/ip sockets
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "tcpip.h"

/* tcpip_connect
 *
 * try to connect to the specified tcp/ip socket, wrap to stdio.h's FILE*
 * structure and turn on line buffering
 */
FILE *
tcpip_connect(const char *hostname, int port)
{
  int sockfd;
  struct sockaddr_in addr;
  struct in_addr inaddr;
  struct hostent *host;
  FILE *handle;
  const char err_connect[] = PACKAGE ": unable to connect to cvs host";

  if(inet_aton(hostname, &inaddr))
    host = gethostbyaddr((char *) &inaddr, sizeof(inaddr), AF_INET);
  else
    host = gethostbyname(hostname);

  if(! host)
    {
      herror(err_connect);
      return NULL;
    }

  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  memcpy(&addr.sin_addr, host->h_addr_list[0], sizeof(addr.sin_addr));

  if((sockfd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {
      perror(err_connect);
      return NULL;
    }

  if(connect(sockfd, (struct sockaddr *) &addr, sizeof(addr)))
    {
      perror(err_connect);
      return NULL;
    }

  handle = fdopen(sockfd, "r+");
  if(! handle)
    {
      perror(err_connect);
      close(sockfd);
      return NULL;
    }

  if(setvbuf(handle, NULL, _IOLBF, 0))
    {
      perror(err_connect);
      fclose(handle);
      return NULL;
    }

  return handle;
}


