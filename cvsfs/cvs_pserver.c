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

#include <string.h>
#include <malloc.h>

#include "cvsfs.h"
#include "cvs_pserver.h"
#include "tcpip.h"

static unsigned char pserver_encrypt_tab[] = 
  {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
   16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
  114,120, 53, 79, 96,109, 72,108, 70, 64, 76, 67,116, 74, 68, 87,
  111, 52, 75,119, 49, 34, 82, 81, 95, 65,112, 86,118,110,122,105,
   41, 57, 83, 43, 46,102, 40, 89, 38,103, 45, 50, 42,123, 91, 35,
  125, 55, 54, 66,124,126, 59, 47, 92, 71,115, 78, 88,107,106, 56,
   36,121,117,104,101,100, 69, 73, 99, 63, 94, 93, 39, 37, 61, 48,
   58,113, 32, 90, 44, 98, 60, 51, 33, 97, 62, 77, 84, 80, 85,223,
  225,216,187,166,229,189,222,188,141,249,148,200,184,136,248,190,
  199,170,181,204,138,232,218,183,255,234,220,247,213,203,226,193,
  174,172,228,252,217,201,131,230,197,211,145,238,161,179,160,212,
  207,221,254,173,202,146,224,151,140,196,205,130,135,133,143,246,
  192,159,244,239,185,168,215,144,139,165,180,157,147,186,214,176,
  227,231,219,169,175,156,206,198,129,164,150,210,154,177,134,127,
  182,128,158,208,162,132,167,209,149,241,153,251,237,236,171,195,
  243,233,253,240,194,250,191,155,142,137,245,235,163,242,178,152
  };


static const char *cvs_pserver_encrypt(const char *);

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

  /* okay, now let's talk a little pserver dialect to log in ... */
  fprintf(cvs_handle, "BEGIN AUTH REQUEST\n");
  fprintf(cvs_handle, "%s\n%s\n%s\n", 
	  config->cvs_root,
	  config->cvs_username,
	  cvs_pserver_encrypt(config->cvs_password));
  fprintf(cvs_handle, "END AUTH REQUEST\n");

  /* the result of our login request is handled in cvs_connect()
   * since this is equal to all supported protocols
   */
  return cvs_handle;
}



/* cvs_pserver_encrypt
 * 
 * encrypt cvs-pserver password
 */
static const char *
cvs_pserver_encrypt(const char *plain_pw)
{
  static char *buf = NULL, *ptr;
  int len = strlen(plain_pw);

  buf = realloc(buf, len + 2);
  if(! buf) return NULL;

  buf[0] = 'A'; /* tell, that we're using pserver password */

  ptr = &buf[1];
  do
    *ptr = pserver_encrypt_tab[(int)*plain_pw];
  while(*ptr && plain_pw ++ && ptr ++);

  return buf;
}

