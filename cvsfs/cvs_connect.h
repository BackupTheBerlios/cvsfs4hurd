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

/* Try connecting to the cvs host specified in 'config'. Return a
 * line-buffer libc FILE* handle on success, NULL if something failed.
 */
FILE *cvs_connect(cvsfs_config *config);

/* read one line from cvs server and make sure, it's an ok message. else
 * call cvs_treat_error. return 0 on 'ok'.
 */
int cvs_wait_ok(FILE *cvs_handle);

/* notify the user (aka log to somewhere) that we've received some error
 * messages, we don't know how to handle ...
 */
void cvs_treat_error(FILE *cvs_handle, char *read_msg);

#endif /* CVS_CONNECT_H */
