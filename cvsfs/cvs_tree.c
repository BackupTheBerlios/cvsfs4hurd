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

#define PACKAGE "cvsfs"

static struct netnode *cvs_tree_enqueue(struct netnode *, const char *);

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
  FILE *send, *recv;
  struct netnode *cwd = (void *) 0xDEADBEEF;
  char *ptr;
  char buf[4096]; /* 4k should be enough for most cvs repositories, if
		   * cvsfs tell's you to increase this value, please do so.
		   */

  if(cvs_connect(&send, &recv))
    return EIO;

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
	  return 0;
	}

      if(! strncmp(buf, "error", 5))
	{
	  cvs_connection_release(send, recv);
	  return EIO;
	}

      if(buf[1] != ' ') 
	{
	  cvs_treat_error(recv, buf);
	  cvs_connection_release(send, recv);
	  return EIO;
	}

      switch(buf[0])
	{
	case 'E': /* E cvs rdiff: Diffing <directory> */
	  if(! (ptr = strstr(buf, "Diffing ")))
	    {
	      cvs_treat_error(recv, buf);
	      cvs_connection_release(send, recv);
	      return EIO;
	    }

	  ptr += 8;
	  if(! *rootdir) 
	    cwd = *rootdir = cvs_tree_enqueue(NULL, ptr);
	  else 
	    cwd = cvs_tree_enqueue(*rootdir, ptr);

	  if(! cwd)
	    {
	      cvs_connection_kill(send, recv);
	      return ENOMEM;
	    }
	  
	  break;

	case 'M': /* M File <file> is new; HEAD revision <revision> */
	  if(! (ptr = strstr(buf, "File ")))
	    {
	      cvs_treat_error(recv, buf);
	      cvs_connection_release(send, recv);
	      return EIO;
	    }
	  
	  {
	    struct netnode *entry;
	    const char *revision;
	    const char *filename = (ptr += 5);

	    if(! (ptr = strstr(filename, " is new")))
	      {
		cvs_treat_error(recv, buf);
		cvs_connection_release(send, recv);
		return EIO;
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
		return EIO;
	      }
	  
	    revision = (ptr += 9);

	    entry = malloc(sizeof(*entry));
	    if(! entry)
	      {
		perror(PACKAGE);
		cvs_connection_kill(send, recv);
		return ENOMEM; /* pray for cvsfs to survive! */
	      }

	    entry->name = strdup(filename);
	    entry->sibling = cwd->child;
	    entry->child = NULL;
	    entry->parent = cwd;
	    entry->fileno = next_fileno ++;

	    /* create lock entry for our new netnode, as it is not linked
	     * to somewhere and this is the only thread to update tree info,
	     * we don't have to write lock to access entry->revision!
	     */
	    rwlock_init(&entry->lock);

	    /* okay, create an initial (mostly empty) revision entry */
	    entry->revision = malloc(sizeof(*entry->revision));
	    if(! entry->revision)
	      {
		free(entry->name);
		free(entry);

		cvs_connection_kill(send, recv);
		return ENOMEM; /* pray for cvsfs to survive! */
	      }

	    entry->revision->id = strdup(revision);
	    entry->revision->contents = NULL;
	    entry->revision->next = NULL;
	    rwlock_init(&entry->revision->lock);

	    cwd->child = entry;
	    break;
	  }

	default:
	  cvs_treat_error(recv, buf);
	  cvs_connection_release(send, recv);
	  return EIO; 
	}
    }

  cvs_connection_kill(send, recv);
  return EIO;
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

  while((end = strchr(path, '/'))) 
    {
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
	  /* this MUST NOT happen, if it occurs anyways, the seems to be
	   * something wrong with our cvs server!
	   */
	  fprintf(stderr, PACKAGE ": unable to find directory '%s'\n", path);
	  return NULL;
	}

      path = end + 1;
      dir = (parent = dir)->child;
    }

  /* okay, create new directory structure right in place ... */
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

  rwlock_init(&new->lock);
  
  if(parent)
    parent->child = new;

  return new;
}
