/**********************************************************
 * node.h
 *
 * Copyright 2004, Stefan Siegl <ssiegl@gmx.de>, Germany
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Publice License,
 * version 2 or any later. The license is contained in the COPYING
 * file that comes with the cvsfs4hurd distribution.
 *
 * code related to handling (aka create, etc.) netfs nodes
 */

#ifndef NODE_H
#define NODE_H

/* create a struct node* for the specified netnode 'nn'.   */
struct node *cvsfs_make_node(struct netnode *);

#endif /* NODE_H */
