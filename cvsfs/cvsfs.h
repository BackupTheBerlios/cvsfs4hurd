/**********************************************************
 * cvsfs.h
 *
 * Copyright (C) 2004, 2005 by Stefan Siegl <ssiegl@gmx.de>, Germany
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Publice License,
 * version 2 or any later. The license is contained in the COPYING
 * file that comes with the cvsfs4hurd distribution.
 *
 * configuration structures
 */

#ifndef CVSFS_CONFIG_H
#define CVSFS_CONFIG_H

#include <maptime.h>
extern volatile struct mapped_time_value *cvsfs_maptime;

#include <stdio.h>
#include <rwlock.h>


typedef struct {
  enum { PSERVER, EXT, LOCAL } cvs_mode;
  char *cvs_shell_client; /* program to use for :ext: connection */

  char *cvs_hostname;
  int cvs_port; /* port no. in localhost's endianess */

  char *cvs_username;
  char *cvs_password;

  char *cvs_root;
  char *cvs_module;

  char *homedir; /* homedir of user (location of .cvspass file) */

  /* whether or whether not the user wants to have no true stats information,
   * this would save downloading revisions just to have the timestamp and
   * permissions set correctly.
   */
  unsigned nostats :1;

#if HAVE_LIBZ
  /* which gzip level to use for file requests */
  unsigned gzip_level :4;
#endif

  /* file-handle to write debug information out to,
   * NULL, if no debug info is to be written out */
  FILE *debug_port;

} cvsfs_config;
extern cvsfs_config config;



typedef struct {
  __uid_t uid;
  __gid_t gid;
  __uid_t author;
  __fsid_t fsid;
  __mode_t mode;
} cvsfs_stat_template;
extern cvsfs_stat_template stat_template;



struct revision;
struct revision {
  /* revision id, something like 1.14 or 1.2.1.12 */
  char *id;

  /* this revisions access permissions */
  mode_t perm;

  /* revision's mtime stamp */
  time_t time;

  /* length of contents field */
  size_t length;

  /* pointer to this revision's contents */
  char *contents;

  /* pointer to the next revision structure, if there are multiple
   * revisions available locally
   */
  struct revision *next;

  /* locking mechanism for the revision structure, needs to be held,
   * on read/write access to contents field.
   */
  struct rwlock lock;
};



struct netnode;
struct netnode {
  /* name of this node, aka file or directory */
  char *name; 

  /* link to the next file or directory, within this directory */
  struct netnode *sibling;

  /* link to the first child of this directory, this points to the second
   * child via it's sibling pointer. NULL, if either this directory is empty
   * or this node represents a file
   */
  struct netnode *child;

  /* link to the parent netnode of this file or directory */
  struct netnode *parent;

  /* head revision number of this netnode, NULL to show, that this node
   * represents a directory!
   */ 
  struct revision *revision;

  /* inode number, assigned to this netnode structure */
  unsigned int fileno;

  /* pointer to node structure, assigned to this netnode */
  struct node *node;

  /* locking mechanism for the netnode. this needs to be held whenever touching
   * the revisions tree (the linking), access to revision to check whether it
   * is NULL (and therefore a directory) doesn't need to be locked.
   * for the revision structure itself there
   * is a separate lock inside each struct revision.
   *
   * furthermore access to node pointer must be locked.
   */
  struct rwlock lock;
};

/* pointer to root netnode */
extern struct netnode *rootdir;



/* helper macros for debugging ****/
#define DEBUG(cat,msg...) \
  if(config.debug_port) \
    fprintf(config.debug_port, PACKAGE ": " cat ": " msg);

#define FUNC_PROLOGUE_(func_name, fmt...) \
  do \
    { \
      const char *debug_func_name = func_name; \
      DEBUG("tracing", "entering %s (" __FILE__ ":%d) ", \
	    debug_func_name, __LINE__); \
      if(config.debug_port) \
        { \
          fmt; \
	  fprintf(config.debug_port, "\n"); \
	}

#define FUNC_PROLOGUE(func_name) \
  FUNC_PROLOGUE_(func_name, (void)0)

#define FUNC_PROLOGUE_FMT(func_name, fmt...) \
  FUNC_PROLOGUE_(func_name, fprintf(config.debug_port, fmt))

#define FUNC_PROLOGUE_NODE(func_name, node) \
  FUNC_PROLOGUE_FMT(func_name, "node=%s", (node)->nn->name)

#define FUNC_EPILOGUE_NORET() \
      DEBUG("tracing", "leaving %s\n", debug_func_name); \
    } while(0);

#define FUNC_RETURN_(ret, fmt) \
      { \
        int retval = (ret); \
        DEBUG("tracing", "leaving %s (" __FILE__ ":%d) ret=%d ", \
	      debug_func_name, __LINE__, retval); \
        if(config.debug_port) \
          { \
	    fmt; \
	    fprintf(config.debug_port, "\n"); \
	  } \
        return retval; \
      }

#define FUNC_EPILOGUE_(ret, fmt) \
      FUNC_RETURN_(ret, fmt) \
    } while(0);

#define FUNC_RETURN_FMT(ret, fmt...) \
  FUNC_RETURN_(ret, fprintf(config.debug_port, fmt))

#define FUNC_EPILOGUE_FMT(ret, fmt...) \
  FUNC_EPILOGUE_(ret, fprintf(config.debug_port, fmt))

#define FUNC_RETURN(ret) \
  FUNC_RETURN_(ret, (void)0)

#define FUNC_EPILOGUE(ret) \
  FUNC_EPILOGUE_(ret, (void)0)

#endif /* CVSFS_CONFIG_H */
