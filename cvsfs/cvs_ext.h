/**********************************************************
 * cvs_ext.h
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

#ifndef CVS_EXT_H
#define CVS_EXT_H

#include <stdio.h>
#include "cvsfs.h"

/* connect to the cvs :ext: server.
 * return 0 on success, only in that case send and recv are guaranteed to
 * be valid. send and recv are already set up to be line buffered.
 */
error_t cvs_ext_connect(FILE **send, FILE **recv);

#endif /* CVS_EXT_H */
