/**********************************************************
 * cvs_connect.h
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

#ifndef CVS_CONNECT_H
#define CVS_CONNECT_H

#include <stdio.h>
#include "cvsfs.h"

/* initialize cvs_connect stuff, this must be called before using cvs_connect
 */
void cvs_connect_init(void);

/* Try connecting to the cvs host. Return 0 on success. 
 * FILE* handles to send and receive are guaranteed to be valid only, if zero
 * status was returned. The handles are line-buffered.
 */
error_t cvs_connect(FILE **send, FILE **recv);

/* Try connecting to cvs host, like cvs_connect, but make a fresh connection
 */
error_t cvs_connect_fresh(FILE **send, FILE **recv);

/* release the connection.  the connection may then either be cached
 * and reused on next cvs_connect() or may be closed.
 */
void cvs_connection_release(FILE *send, FILE *recv);

/* release the connection but don't ever try to cache it.  You want to
 * call this in case you stumbled over an error you don't know how to
 * recover from automatically or simple got EOF
 */
#define cvs_connection_kill(send, recv) \
  do { fclose(send); if(send != recv) fclose(recv); } while(0)

/* read one line from cvs server and make sure, it's an ok message. else
 * call cvs_treat_error. return 0 on 'ok'.
 */
error_t cvs_wait_ok(FILE *recv_handle);

/* notify the user (aka log to somewhere) that we've received some error
 * messages, we don't know how to handle ...
 */
void cvs_treat_error(FILE *recv_handle, char *read_msg);

#endif /* CVS_CONNECT_H */
