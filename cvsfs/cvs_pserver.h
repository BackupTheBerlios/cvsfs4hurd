/**********************************************************
 * cvs_pserver.h
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

#ifndef CVS_PSERVER_H
#define CVS_PSERVER_H

#include <stdio.h>
#include "cvsfs.h"

/* connect to the cvs pserver */
error_t cvs_pserver_connect(FILE **send, FILE **recv);

#endif /* CVS_PSERVER_H */
