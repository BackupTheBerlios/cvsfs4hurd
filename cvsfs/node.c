/**********************************************************
 * node.c
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "cvsfs.h"
#include "node.h"

#include <hurd/netfs.h>
#include <assert.h>
#include <stdio.h>

/* next file number (aka inode) we will assign */
extern volatile unsigned int next_fileno;


/* cvsfs_make_node
 *
 * create a struct node* for the specified netnode 'nn'. 
 */
struct node *
cvsfs_make_node(struct netnode *nn)
{
  struct node *node;

  rwlock_writer_lock(&nn->lock);

  if(nn->node) 
    {
      /* there already is a node structure, just return another reference
       * to this one, instead of wasting memory for yet another one
       */
      mutex_lock(&nn->node->lock);
      netfs_nref(nn->node);
      mutex_unlock(&nn->node->lock);

      rwlock_writer_unlock(&nn->lock);
      return nn->node;
    }

  if(! (node = netfs_make_node(nn)))
    {
      rwlock_writer_unlock(&nn->lock);
      return NULL;
    }

  /* put timestamp on file */
  fshelp_touch(&node->nn_stat,
	       TOUCH_ATIME | TOUCH_MTIME | TOUCH_CTIME, cvsfs_maptime);

  /* initialize stats of new node ... */
  node->nn_stat.st_fstype = FSTYPE_MISC;
  node->nn_stat.st_fsid = stat_template.fsid;
  node->nn_stat.st_ino = nn->fileno;
  node->nn_stat.st_mode = stat_template.mode;
  node->nn_stat.st_nlink = 1;
  node->nn_stat.st_uid = stat_template.uid;
  node->nn_stat.st_gid = stat_template.gid;
  node->nn_stat.st_size = 0;
  node->nn_stat.st_author = stat_template.author;

  if(! nn->revision)
    {
      /* we're creating a node for a directory, mark as such! */
      node->nn_stat.st_mode |= S_IFDIR;

      /* since we got a directory we need to supply "executable" 
       * permissions, so our user is enabled to make use of this dir
       */
      if(node->nn_stat.st_mode & S_IRUSR)
	node->nn_stat.st_mode |= S_IXUSR;

      if(node->nn_stat.st_mode & S_IRGRP)
	node->nn_stat.st_mode |= S_IXGRP;

      if(node->nn_stat.st_mode & S_IROTH)
	node->nn_stat.st_mode |= S_IXOTH;
    }
  else
    {
      if(nn->revision->contents)
	{
	  node->nn_stat.st_mode = nn->revision->perm;
	  node->nn_stat.st_size = nn->revision->length;

	  node->nn_stat.st_atime =
	    node->nn_stat.st_mtime =
	    node->nn_stat.st_ctime = nn->revision->time;

	  node->nn_stat.st_atime_usec =
	    node->nn_stat.st_mtime_usec =
	    node->nn_stat.st_ctime_usec = 0;
	}

      /* well, we're creating a new node for a file ... */
      node->nn_stat.st_mode |= S_IFREG;

      /* for now simply drop all execute permissions, this needs to be fixed,
       * since CVS support executables, e.g. shell scripts, that we need to
       * support .... FIXME
       */
      node->nn_stat.st_mode &= ~(S_IXUSR | S_IXGRP | S_IXOTH);
    }

  /* cvsfs is currently read only, check-ins etc. aren't yet supported,
   * therefore drop permission to write 
   */
  node->nn_stat.st_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);

  nn->node = node;
  rwlock_writer_unlock(&nn->lock);

  return node;
}



struct node *
cvsfs_make_virtual_node(struct netnode *nn, struct revision *rev)
{
  struct node *node;
  struct netnode *new_nn;

  if(! nn->revision) 
    return NULL; /* we don't create virtual nodes for directories */

  /* we need a virtual netnode structure, pointing to the revision
   * of choice ...
   */
  new_nn = malloc(sizeof(*new_nn));

  if(! new_nn)
    return NULL;

  rwlock_init(&new_nn->lock);

  new_nn->sibling = NULL;
  new_nn->parent = NULL;
  new_nn->node = NULL; /* will be assigned by cvsfs_make_node */
  new_nn->name = rev->id;
  new_nn->revision = rev;
  new_nn->fileno = next_fileno ++;

  /* keep a pointer to the real nn structure in new_nn->child
   * this is needed if we want to retrieve a version controlled file, since
   * we got to climb up the whole path then ...
   */
  new_nn->child = nn;

  if(! (node = cvsfs_make_node(new_nn)))
    {
      free(new_nn);
      return NULL;
    }

  return node;
}
