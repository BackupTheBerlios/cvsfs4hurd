/**********************************************************
 * cvs_tree.c
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "cvs_connect.h"
#include "cvs_tree.h"

static struct netnode *cvs_tree_enqueue(struct netnode *, const char *);

/* check whether there already is a netnode for the file with the provided
 * name, create a new one, if not. add revision information for HEAD revision.
 */
static error_t cvs_tree_enqueue_file(struct netnode *cwd, const char *filename,
				     const char *revision);


/* next file number (aka inode) we will assign */
volatile unsigned int next_fileno = 1;


/* netnode *cvs_tree_read
 *
 * read the whole file and directory tree of the module specified in config
 * structure.  The tree is stored in **ptr_to_rootnode, make sure
 * *ptr_to_rootnode is NULL on first call.
 * RETURN: 0 on success
 */
error_t
cvs_tree_read(struct netnode **rootdir)
{
  FUNC_PROLOGUE("cvs_tree_read");
  FILE *send, *recv;
  struct netnode *cwd = (void *) 0xDEADBEEF;
  char *ptr;
  char buf[4096]; /* 4k should be enough for most cvs repositories, if
		   * cvsfs tell's you to increase this value, please do so.
		   */

  if(cvs_connect(&send, &recv))
    FUNC_RETURN(EIO);

  fprintf(send, 
	  "UseUnchanged\n"
	  "Argument -s\n" /* we don't want to download the file's contents */
	  "Argument -r\nArgument 0\n"
	  "Argument -r\nArgument HEAD\n"
	  "Argument %s\n"
	  "rdiff\n", config.cvs_module);

  /* cvs now either answers like this:
   * E cvs rdiff: Diffing <directory>
   * M File <file> is new; HEAD revision <revision>
   *
   * the other possibility is as follows:
   * E cvs rdiff: cannot find module <module> - ignored
   * error
   */
  while(fgets(buf, sizeof(buf), recv))
    {
      ptr = buf + strlen(buf);
      ptr --;

      if(*ptr != 10)
	{
	  fprintf(stderr, PACKAGE "cvs_tree_read's parse buffer is "
		  "too small, stop for the moment.\n");
	  exit(10);
	}

      /* chop the linefeed off the end */
      *ptr = 0;

      if(! strncmp(buf, "ok", 2))
	{
	  cvs_connection_release(send, recv);
	  FUNC_RETURN(0);
	}

      if(! strncmp(buf, "error", 5))
	{
	  cvs_connection_release(send, recv);
	  FUNC_RETURN(EIO);
	}

      if(buf[1] != ' ') 
	{
	  cvs_treat_error(recv, buf);
	  cvs_connection_release(send, recv);
	  FUNC_RETURN(EIO);
	}

      DEBUG("tree-read", "%s\n", buf);
      switch(buf[0])
	{
	case 'E': /* E cvs rdiff: Diffing <directory> */
	  if(! (ptr = strstr(buf, "Diffing ")))
	    {
	      cvs_treat_error(recv, buf);
	      cvs_connection_release(send, recv);
	      FUNC_RETURN(EIO);
	    }

	  ptr += 8;
	  if(! *rootdir) 
	    cwd = *rootdir = cvs_tree_enqueue(NULL, ptr);
	  else 
	    cwd = cvs_tree_enqueue(*rootdir, ptr);

	  if(! cwd)
	    {
	      cvs_connection_kill(send, recv);
	      FUNC_RETURN(ENOMEM);
	    }
	  
	  break;

	case 'M': /* M File <file> is new; HEAD revision <revision> */
	  if(! (ptr = strstr(buf, "File ")))
	    {
	      cvs_treat_error(recv, buf);
	      cvs_connection_release(send, recv);
	      FUNC_RETURN(EIO);
	    }
	  
	  {
	    const char *revision;
	    const char *filename = (ptr += 5);

	    if(! (ptr = strstr(filename, " is new")))
	      {
		cvs_treat_error(recv, buf);
		cvs_connection_release(send, recv);
		FUNC_RETURN(EIO);
	      }
	    *(ptr ++) = 0;

	    revision = ptr;

	    /* strip leading path from filename */
	    while((ptr = strchr(filename, '/')))
	      filename = ptr + 1;

	    if(! (ptr = strstr(revision, "revision ")))
	      {
		cvs_treat_error(recv, NULL);
		cvs_connection_release(send, recv);
		FUNC_RETURN(EIO);
	      }
	  
	    revision = (ptr += 9);

	    if(cvs_tree_enqueue_file(cwd, filename, revision))
	      {
		cvs_connection_kill(send, recv);
		FUNC_RETURN(ENOMEM);
	      }

	    break;
	  }

	default:
	  cvs_treat_error(recv, buf);
	  cvs_connection_release(send, recv);
	  FUNC_RETURN(EIO);
	}
    }

  cvs_connection_kill(send, recv);
  FUNC_EPILOGUE(EIO);
}


/* cvs_tree_enqueue(netnode, path)
 *
 * allocate an empty netnode structure for the directory addressed by
 * the argument 'path' and put it into the netnode structure *dir
 */
static struct netnode *
cvs_tree_enqueue(struct netnode *dir, const char *path)
{
  struct netnode *new, *parent = NULL;
  char *end;

  DEBUG("tree-enqueue", "root=%s, path=%s\n", dir ? dir->name : NULL, path);

  if(! (end = strchr(path, '/')))
    {
      /* request for root directory, else there would be a '/' within
       * path. return existing rootdir (dir), if available.
       */
      if(dir) {
	if(! strcmp(dir->name, path))
	  return dir;

	parent = dir;
	dir = (parent = dir)->child;
      }
    }
  else do
    {
      /* if we are in repository browsing mode (i.e. top level module's name
       * is '.', compare root dir's children names first, since the CVS server
       * writes something like CVSROOT/Emptydir (omitting the leading '.')
       */
      if(! strcmp(dir->name, "."))
	dir = dir->child;

      /* now select this directory from within dir (on the current level) */
      if(dir)
	do
	  if(strncmp(dir->name, path, end - path) == 0
	     /* make sure not to match partials: */
	     && dir->name[end - path] == 0)
	    break; /* hey, this is the directory we're looking for! */
	while((dir = dir->sibling));
      
      if(! dir) 
	{
	  /* this MUST NOT happen, if it occurs anyways, there seems to be
	   * something wrong with our cvs server!
	   */
	  fprintf(stderr, PACKAGE ": unable to find directory '%s'\n", path);
	  return NULL;
	}

      path = end + 1;
      dir = (parent = dir)->child;
    }
  while((end = strchr(path, '/')));

  /* scan parent directory for the entry we're looking for ... */
  if(parent)
    for(new = parent->child; new; new = new->sibling)
      if(! strcmp(new->name, path))
	return new;

  /* okay, create new directory structure right in place ... */
  DEBUG("tree-enqueue", "adding new node: parent=%s, path=%s\n",
	parent ? parent->name : NULL, path);

  new = malloc(sizeof(*new));
  if(! new)
    {
      perror(PACKAGE);
      return NULL; /* pray for cvsfs to survive! */
    }

  new->name = strdup(path);
  new->sibling = dir;
  new->child = NULL;
  new->parent = parent;
  new->revision = NULL; /* mark as a directory */
  new->fileno = next_fileno ++;
  new->node = NULL;

  rwlock_init(&new->lock);
  
  if(parent)
    parent->child = new;

  return new;
}


/* cvs_tree_enqueue_file
 *
 * check whether there already is a netnode for the file with the provided
 * name, create a new one, if not. add revision information for HEAD revision.
 */
static error_t
cvs_tree_enqueue_file(struct netnode *cwd,
		      const char *filename, const char *revision)
{
  struct netnode *entry;

  /* cvs_tree_add_rev_struct
   * add a mostly empty revision structure to the specified netnode
   */
  error_t cvs_tree_add_rev_struct(struct netnode *entry, const char *revision)
    {
      struct revision *cached_rev;

      rwlock_writer_lock(&entry->lock);
      cached_rev = entry->revision;

      if(! (entry->revision = malloc(sizeof(*entry->revision))))
	{
	  rwlock_writer_unlock(&entry->lock);
	  return ENOMEM; /* pray for cvsfs to survive! */
	}

      entry->revision->id = strdup(revision);
      entry->revision->contents = NULL;
      entry->revision->next = cached_rev;

      rwlock_init(&entry->revision->lock);
      rwlock_writer_unlock(&entry->lock);

      return 0;
    }

  /* well, first scan directory tree whether we already have 
   * the file we're looking for ... 
   */
  for(entry = cwd->child; entry; entry = entry->sibling)
    if(! strcmp(entry->name, filename))
      {
	/* okay, we already got a netnode for file 'filename', check whether
	 * revision information is up to date ...
	 */
	rwlock_reader_lock(&entry->lock);
	if(! strcmp(revision, entry->revision->id))
	  {
	    rwlock_reader_unlock(&entry->lock);
	    return 0; /* head revision id hasn't changed ... */
	  }
	rwlock_reader_unlock(&entry->lock);

	/* okay, create new revision struct */
	if(cvs_tree_add_rev_struct(entry, revision))
	  return ENOMEM;

	return 0;
      }

  /* okay, don't have this particular file available,
   * put a new netnode together ...
   */
  if(! (entry = malloc(sizeof(*entry))))
    {
      perror(PACKAGE);
      return ENOMEM; /* pray for cvsfs to survive! */
    }

  entry->name = strdup(filename);
  entry->sibling = cwd->child;
  entry->child = NULL;
  entry->parent = cwd;
  entry->fileno = next_fileno ++;
  entry->node = NULL;

  /* create lock entry for our new netnode, as it is not linked
   * to somewhere and this is the only thread to update tree info,
   * we don't have to write lock to access entry->revision!
   */
  rwlock_init(&entry->lock);

  entry->revision = NULL;
  if(cvs_tree_add_rev_struct(entry, revision))
    {
      perror(PACKAGE);
      free(entry->name);
      free(entry);
      return ENOMEM;
    }

  /* do this as late as possible, aka only if the full entry structure
   * is valid, since we do not lock the netnode -- however we're in the
   * only thread touching the tree at all
   */
  cwd->child = entry;

  return 0;
}
