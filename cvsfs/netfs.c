/**********************************************************
 * netfs.c
 *
 * Copyright 2004, Stefan Siegl <ssiegl@gmx.de>, Germany
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Publice License,
 * version 2 or any later. The license is contained in the COPYING
 * file that comes with the cvsfs4hurd distribution.
 *
 * callback functions for libnetfs
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#define PACKAGE "cvsfs"
#define VERSION "0.1"


#include <stddef.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include <hurd/netfs.h>

#include <stdio.h>

#include "cvsfs.h"
#include "cvs_files.h"
#include "node.h"



/* Make sure that NP->nn_stat is filled with current information.  CRED
   identifies the user responsible for the operation.  */
error_t
netfs_validate_stat (struct node *node, struct iouser *cred)
{
  return 0;
}



/* Read the contents of NODE (a symlink), for USER, into BUF. */
error_t netfs_attempt_readlink (struct iouser *user, struct node *node,
				char *buf)
{
  /* actually we don't have no symlinks in cvsfs, at least not for
   * the time being
   */
  return EINVAL;
}



/* Attempt to create a file named NAME in DIR for USER with MODE.  Set *NODE
   to the new node upon return.  On any error, clear *NODE.  *NODE should be
   locked on success; no matter what, unlock DIR before returning.  */
error_t
netfs_attempt_create_file (struct iouser *user, struct node *dir,
			   char *name, mode_t mode, struct node **node)
{
  *node = 0;
  mutex_unlock (&dir->lock);
  return EROFS;
}



/* This should attempt a chmod call for the user specified by CRED on node
   NODE, to change the owner to UID and the group to GID. */
error_t netfs_attempt_chown (struct iouser *cred, struct node *node,
			     uid_t uid, uid_t gid)
{
  return EROFS;
}



/* This should attempt to fetch filesystem status information for the remote
   filesystem, for the user CRED. */
error_t
netfs_attempt_statfs (struct iouser *cred, struct node *node,
		      fsys_statfsbuf_t *st)
{
  return EOPNOTSUPP;
}



/* Attempt to create a new directory named NAME in DIR for USER with mode
   MODE.  */
error_t netfs_attempt_mkdir (struct iouser *user, struct node *dir,
			     char *name, mode_t mode)
{
  return EROFS;
}



/* This should attempt a chflags call for the user specified by CRED on node
   NODE, to change the flags to FLAGS. */
error_t netfs_attempt_chflags (struct iouser *cred, struct node *node,
			       int flags)
{
  return EROFS;
}



/* Node NODE is being opened by USER, with FLAGS.  NEWNODE is nonzero if we
   just created this node.  Return an error if we should not permit the open
   to complete because of a permission restriction. */
error_t
netfs_check_open_permissions (struct iouser *user, struct node *node,
			      int flags, int newnode)
{
  error_t err = 0;

  if (flags & O_READ)
    err = fshelp_access (&node->nn_stat, S_IREAD, user);

  if (!err && (flags & O_WRITE))
    err = fshelp_access (&node->nn_stat, S_IWRITE, user);

  if (!err && (flags & O_EXEC))
    err = fshelp_access (&node->nn_stat, S_IEXEC, user);

  return err;
}



/* This should attempt a chmod call for the user specified by CRED on node
   NODE, to change the mode to MODE.  Unlike the normal Unix and Hurd meaning
   of chmod, this function is also used to attempt to change files into other
   types.  If such a transition is attempted which is impossible, then return
   EOPNOTSUPP.  */
error_t netfs_attempt_chmod (struct iouser *cred, struct node *node,
			     mode_t mode)
{
  return EROFS;
}



/* Attempt to create an anonymous file related to DIR for USER with MODE.
   Set *NODE to the returned file upon success.  No matter what, unlock DIR. */
error_t netfs_attempt_mkfile (struct iouser *user, struct node *dir,
			      mode_t mode, struct node **node)
{
  *node = 0;
  mutex_unlock (&dir->lock);
  return EROFS;
}



/* This should sync the entire remote filesystem.  If WAIT is set, return
   only after sync is completely finished.  */
error_t netfs_attempt_syncfs (struct iouser *cred, int wait)
{
  /* we don't support writing */
  return 0;
}



/* This should sync the file NODE completely to disk, for the user CRED.  If
   WAIT is set, return only after sync is completely finished.  */
error_t
netfs_attempt_sync (struct iouser *cred, struct node *node, int wait)
{
  /* we don't support writing to files, therefore syncing isn't really
   * much to worry about ...
   */
  return 0;
}



/* Delete NAME in DIR for USER. */
error_t netfs_attempt_unlink (struct iouser *user, struct node *dir,
			      char *name)
{
  return EROFS;
}



/* This should attempt to set the size of the file NODE (for user CRED) to
   SIZE bytes long. */
error_t netfs_attempt_set_size (struct iouser *cred, struct node *node,
				loff_t size)
{
  return EOPNOTSUPP;
}



/* Attempt to turn NODE (user CRED) into a device.  TYPE is either S_IFBLK or
   S_IFCHR. */
error_t netfs_attempt_mkdev (struct iouser *cred, struct node *node,
			     mode_t type, dev_t indexes)
{
  return EROFS;
}



/* Return the valid access types (bitwise OR of O_READ, O_WRITE, and O_EXEC)
   in *TYPES for file NODE and user CRED.  */
error_t
netfs_report_access (struct iouser *cred, struct node *node, int *types)
{
  *types = 0;

  if (fshelp_access (&node->nn_stat, S_IREAD, cred) == 0)
    *types |= O_READ;
  
  /* we don't support writing to files, therefore don't even think of
   * returning writable ...
   *
   * if (fshelp_access (&node->nn_stat, S_IWRITE, cred) == 0)
   *   *types |= O_WRITE;
   */

  if (fshelp_access (&node->nn_stat, S_IEXEC, cred) == 0)
    *types |= O_EXEC;
  
  return 0;
}



/* Lookup NAME in DIR for USER; set *NODE to the found name upon return.  If
   the name was not found, then return ENOENT.  On any error, clear *NODE.
   (*NODE, if found, should be locked, this call should unlock DIR no matter
   what.) */
error_t netfs_attempt_lookup (struct iouser *user, struct node *dir,
			      char *name, struct node **node)
{
  error_t err = ENOENT;
  struct netnode *nn = dir->nn->child;

  if(! strcmp(name, "."))
    {
      /* return another reference to this directory! */
      netfs_nref(dir);
      *node = dir;
      err = 0;
    }
  else if(! strcmp(name, ".."))
    {
      if(dir->nn->parent)
	{
	  /* return a reference to our parent */
	  *node = cvsfs_make_node(dir->nn->parent);
	  err = 0;
	}
      else
	/* this is the root directory of cvsfs, but the user 
	 * requests to go up by one. we can't tell, where to go, so ...
	 */
	err = EAGAIN;
    }
  else
    {
      for(; nn; nn = nn->sibling)
	if(! strcmp(nn->name, name)) 
	  {
	    err = 0; /* hey, we got it! */

	    if(nn->node)
	      {
		*node = nn->node;
		netfs_nref(nn->node);
	      }
	    else 
	      *node = cvsfs_make_node(nn);

	    break;
	  }
    }

  mutex_unlock(&dir->lock);

  if(err)
    *node = NULL;

  return err;
}



/* Create a link in DIR with name NAME to FILE for USER.  Note that neither
   DIR nor FILE are locked.  If EXCL is set, do not delete the target, but
   return EEXIST if NAME is already found in DIR.  */
error_t netfs_attempt_link (struct iouser *user, struct node *dir,
			    struct node *file, char *name, int excl)
{
  return EROFS;
}



/* Attempt to remove directory named NAME in DIR for USER. */
error_t netfs_attempt_rmdir (struct iouser *user,
			     struct node *dir, char *name)
{
  return EROFS;
}



/* This should attempt a chauthor call for the user specified by CRED on node
   NODE, to change the author to AUTHOR. */
error_t netfs_attempt_chauthor (struct iouser *cred, struct node *node,
				uid_t author)
{
  return EROFS;
}



/* Attempt to turn NODE (user CRED) into a symlink with target NAME. */
error_t netfs_attempt_mksymlink (struct iouser *cred, struct node *node,
				 char *name)
{
  return EROFS;
}



/* Note that in this one call, neither of the specific nodes are locked. */
error_t netfs_attempt_rename (struct iouser *user, struct node *fromdir,
			      char *fromname, struct node *todir,
			      char *toname, int excl)
{
  return EROFS;
}



/* Write to the file NODE for user CRED starting at OFSET and continuing for up
   to *LEN bytes from DATA.  Set *LEN to the amount seccessfully written upon
   return. */
error_t netfs_attempt_write (struct iouser *cred, struct node *node,
			     loff_t offset, size_t *len, void *data)
{
  return EROFS;
}



/* This should attempt a utimes call for the user specified by CRED on node
   NODE, to change the atime to ATIME and the mtime to MTIME. */
error_t
netfs_attempt_utimes (struct iouser *cred, struct node *node,
		      struct timespec *atime, struct timespec *mtime)
{
  error_t err = fshelp_isowner (&node->nn_stat, cred);
  int flags = TOUCH_CTIME;
  
  if (! err)
    {
      if (mtime)
	{
	  node->nn_stat.st_mtime = mtime->tv_sec;
	  node->nn_stat.st_mtime_usec = mtime->tv_nsec / 1000;
	}
      else
	flags |= TOUCH_MTIME;
      
      if (atime)
	{
	  node->nn_stat.st_atime = atime->tv_sec;
	  node->nn_stat.st_atime_usec = atime->tv_nsec / 1000;
	}
      else
	flags |= TOUCH_ATIME;
      
      fshelp_touch (&node->nn_stat, flags, cvsfs_maptime);
    }

  return err;
}



/* Read from the file NODE for user CRED starting at OFFSET and continuing for
   up to *LEN bytes.  Put the data at DATA.  Set *LEN to the amount
   successfully read upon return.  */
error_t netfs_attempt_read (struct iouser *cred, struct node *node,
			    loff_t offset, size_t *len, void *data)
{
  int maxlen;

  if(! node->nn->revision)
    {
      fprintf(stderr, "netfs_attempt_read entered, for something not "
	      "being a CVS revision controlled file. getting outta here.\n");
      return EISDIR;
    }

  if(! node->nn->revision->contents) 
    {
      /* we don't have the content of this revision cached locally,
       * therefore try to fetch it.
       *
       * TODO: consider whether it's possible (if using non-blocking I/O)
       * to fork a retrieval task, and return 0 bytes for the time being ..
       */
      if(cvs_files_cache(config.cvs_handle, node->nn, node->nn->revision))
	{
	  fprintf(stderr, "cvs_files_cache call failed, sorry.\n");

	  *len = 0;
	  return EIO;
	}
    }

  maxlen = strlen(node->nn->revision->contents);
  
  if(offset >= maxlen)
    {
      /* trying to read beyond of file, cowardly refuse to do so ... */
      *len = 0;
      return 0;
    }

  if(*len + offset >= maxlen)
    *len = maxlen - offset;

  memcpy(data, node->nn->revision->contents + offset, *len);
  return 0;
}



/* Returned directory entries are aligned to blocks this many bytes long.
   Must be a power of two.  */
#define DIRENT_ALIGN 4
#define DIRENT_NAME_OFFS offsetof (struct dirent, d_name)

/* Length is structure before the name + the name + '\0', all
   padded to a four-byte alignment.  */
#define DIRENT_LEN(name_len)						      \
  ((DIRENT_NAME_OFFS + (name_len) + 1 + (DIRENT_ALIGN - 1))		      \
   & ~(DIRENT_ALIGN - 1))

error_t
netfs_get_dirents (struct iouser *cred, struct node *dir,
		   int first_entry, int num_entries, char **data,
		   mach_msg_type_number_t *data_len,
		   vm_size_t max_data_len, int *data_entries)
{
  error_t err;
  int count;
  size_t size = 0;		/* Total size of our return block.  */
  struct netnode *first_nn, *nn;

  /* Add the length of a directory entry for NAME to SIZE and return true,
     unless it would overflow MAX_DATA_LEN or NUM_ENTRIES, in which case
     return false.  */
  int bump_size (const char *name)
    {
      if (num_entries == -1 || count < num_entries)
	{
	  size_t new_size = size + DIRENT_LEN (strlen (name));
	  if (max_data_len > 0 && new_size > max_data_len)
	    return 0;
	  size = new_size;
	  count++;
	  return 1;
	}
      else
	return 0;
    }

  if(dir->nn->revision)
    return ENOTDIR; /* it's a file ... */

  /* find the first entry, we shall write out to the user ... */
  for(first_nn = dir->nn->child, count = 2; 
      first_nn && first_entry > count;
      first_nn = first_nn->sibling, count ++);

  count = 0;

  /* Make space for the `.' and `..' entries.  */
  if (first_entry == 0)
    bump_size (".");

  if (first_entry <= 1)
    bump_size ("..");

  /* let's see, how much space we need for the result ... */
  for(nn = first_nn; nn; nn = nn->sibling)
    if(! bump_size(nn->name))
      break;

  /*  if(! size)
   *    {
   *      *data = NULL;
   *      *data_len = 0;
   *      *data_entries = 0;
   *      return 0;
   *    }
   */

  /* Allocate it.  */
  *data = mmap (0, size, PROT_READ | PROT_WRITE, MAP_ANON, 0, 0);
  err = ((void *) *data == (void *) -1) ? errno : 0;

  if (! err)
    /* Copy out the result.  */
    {
      char *p = *data;

      int add_dir_entry (const char *name, ino_t fileno, int type)
	{
	  if (num_entries == -1 || count < num_entries)
	    {
	      struct dirent hdr;
	      size_t name_len = strlen (name);
	      size_t sz = DIRENT_LEN (name_len);

	      if (sz > size)
		return 0;
	      else
		size -= sz;

	      hdr.d_fileno = fileno;
	      hdr.d_reclen = sz;
	      hdr.d_type = type;
	      hdr.d_namlen = name_len;

	      memcpy (p, &hdr, DIRENT_NAME_OFFS);
	      strcpy (p + DIRENT_NAME_OFFS, name);
	      p += sz;

	      count++;

	      return 1;
	    }
	  else
	    return 0;
	}

      *data_len = size;
      *data_entries = count;

      count = 0;

      /* Add `.' and `..' entries.  */
      if (first_entry == 0)
	add_dir_entry (".", 2, DT_DIR);
      if (first_entry <= 1)
	add_dir_entry ("..", 2, DT_DIR);

      /* okay, now tell about the real entries ... */
      for(nn = first_nn; nn; nn = nn->sibling)
	if(! add_dir_entry(nn->name, nn->fileno,
			   nn->revision ? DT_REG : DT_DIR))
	  break;
    }

  fshelp_touch (&dir->nn_stat, TOUCH_ATIME, cvsfs_maptime);
  return err;
}



/* Node NP is all done; free all its associated storage. */
void
netfs_node_norefs (struct node *node)
{
  /* the node will be freed, therefore our nn->node pointer will not
   * be valid any longer, therefore reset it 
   */
  node->nn->node = NULL;
}
