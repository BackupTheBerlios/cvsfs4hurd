/**********************************************************
 * cvs_files.h
 *
 * Copyright 2004, Stefan Siegl <ssiegl@gmx.de>, Germany
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Publice License,
 * version 2 or any later. The license is contained in the COPYING
 * file that comes with the cvsfs4hurd distribution.
 *
 * download arbitrary revisions from cvs host and cache them locally
 */

#ifndef CVS_FILES_H
#define CVS_FILES_H

#include <stdio.h>

/* Download the revision (as specified by rev) of the specified file.  */
int cvs_files_cache(struct netnode *file, struct revision *rev);

#endif /* CVS_FILES_H */
