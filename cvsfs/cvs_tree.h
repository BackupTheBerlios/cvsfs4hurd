/**********************************************************
 * cvs_tree.h
 *
 * Copyright 2004, Stefan Siegl <ssiegl@gmx.de>, Germany
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Publice License,
 * version 2 or any later. The license is contained in the COPYING
 * file that comes with the cvsfs4hurd distribution.
 *
 * download file/directory tree from cvs
 */

#ifndef CVS_TREE_H
#define CVS_TREE_H

#include <stdio.h>

/* read the whole file and directory tree of the specified module (root_dir).
 * RETURN: pointer to the root directory, NULL on error
 */
struct netnode *cvs_tree_read(FILE *cvs_handle, const char *root_dir);

#endif /* CVS_TREE_H */
