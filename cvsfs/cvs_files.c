/**********************************************************
 * cvs_files.c
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <time.h>
#include <sys/stat.h>

#include "cvsfs.h"
#include "cvs_files.h"
#include "cvs_connect.h"

/* cvs_files_print_path_recursively
 *
 * recurse from dir upwards to the root node and write out the directories'
 * names to cvs_handle as falling back.
 */
static void
cvs_files_print_path_recursively(FILE *cvs_handle, struct netnode *dir)
{
  if(dir->parent)
    cvs_files_print_path_recursively(cvs_handle, dir->parent);
  fprintf(cvs_handle, "%s/", dir->name);
}


/* cvs_files_cvsattr_to_mode_t
 *
 * convert cvs mode string to posix style mode_t
 */
static mode_t
cvs_files_cvsattr_to_mode_t(const char *ptr)
{
  const mode_t modes[9] =
    { S_IRUSR, S_IWUSR, S_IXUSR,
      S_IRGRP, S_IWGRP, S_IXGRP,
      S_IROTH, S_IWOTH, S_IXOTH
    };
  const mode_t *stage = modes;
  mode_t perm = 0;

  for(;;ptr ++)
    switch(*ptr)
      {
      case 'u': stage = &modes[0]; break;
      case 'g': stage = &modes[3]; break;
      case 'o': stage = &modes[6]; break;
      case '=': break;
      case ',': break;
      case 'r': perm |= stage[0]; break;
      case 'w': perm |= stage[1]; break;
      case 'x': perm |= stage[2]; break;

      default:
	return perm;
      }
}	  



/* cvs_files_cache
 *
 * Download the revision (as specified by rev) of the specified file. 
 */
error_t
cvs_files_cache(struct netnode *file, struct revision *rev)
{
  FILE *send, *recv;
  char buf[1024]; /* 1k should be enough for most cvs repositories, if
		   * cvsfs tell's you to increase this value, please do so.
		   *
		   * TODO in the far future we may have a fgets-thingy, that
		   * allocates it's memory dynamically, which occurs to be
		   * a goodthing(TM) ...
		   */

  if(cvs_connect(&send, &recv))
    return EIO;

  /* write out request header */
  fprintf(send,
	  "Argument -l\n" /* local dir only */
	  "Argument -N\n" /* don't shorten module names */
	  "Argument -P\n" /* no empty directories */
	  "Argument -d\nArgument .\n" /* checkout to local dir */
	  "Argument -r\nArgument %s\n"
	  "Argument --\n"
	  "Argument ",
	  rev->id);

  /* write out pathname from rootnode on ... */
  cvs_files_print_path_recursively(send, file->parent);

  /* last but not least write out the filename */
  fprintf(send, "%s\n", file->name);

  /* okay, send checkout command ... */
  fprintf(send,
	  "Directory .\n%s\n" /* cvsroot */
	  "co\n", config.cvs_root);

  /* example response:
   * *** SERVER ***
   * Mod-time 8 Sep 2004 17:05:43 -0000
   * Updated ./wsdebug/
   * /home/cvs/wsdebug/debug.c
   * /debug.c/1.1///T1.1
   * u=rw,g=rw,o=rw
   * 6285
   * [content]
   */

  while(fgets(buf, sizeof(buf), recv))
    {
      char *ptr;
      int buflen = strlen(buf);

      ptr = buf + buflen;
      ptr --;

      if(*ptr != 10)
	{
	  fprintf(stderr, PACKAGE "cvs_files_cache's parse buffer is "
		  "too small, stopping for the moment.\n");
	  exit(10);
	}

      if(buf[0] == '/')
	continue; /* just file name stuff, as we request only one file
		   * we don't have to care for that ...
		   */

      if(! strncmp(buf, "Mod-time ", 9))
	{
	  struct tm tm;
	  time_t t = 0;

	  memset(&tm, 0, sizeof(tm));
	  if(strptime(&buf[9], "%d %b %Y %T ", &tm))
	    t = mktime(&tm);
	  
	  rev->time = t;
	  continue;
	}

      if(! strncmp(buf, "Updated ", 8))
	continue; /* pathname of parent directory, don't give a fuck ... */

      if(buf[0] == 'M')
	continue; /* probably something like 'M U <filename>' */

      if(buf[0] == 'u' && buf[1] == '=')
	rev->perm = cvs_files_cvsattr_to_mode_t(buf);

      else if(buf[0] >= '0' && buf[0] <= '9')
	{
	  size_t read;
	  size_t length = atoi(buf);
	  size_t bytes = length;
	  char *content = malloc(bytes);
	  char *ptr = content;

	  if(! content)
	    {
	      perror(PACKAGE);
	      cvs_connection_kill(send, recv);
	      return ENOMEM;
	    }

	  while(bytes && (read = fread(ptr, 1, bytes, recv)))
	    {
	      bytes -= read;
	      ptr += read;
	    }

	  if(bytes)
	    {
	      /* unable to read all data ... */
	      fprintf(stderr, "unable to read whole file %s\n", file->name);
	      free(content);
	      cvs_connection_kill(send, recv);
	      return EIO;
	    }

	  free(rev->contents);
	  rev->length = length;
	  rev->contents = content;
	}
      else if(! strncmp(buf, "ok", 2))
	{
	  cvs_connection_release(send, recv);
	  return 0; /* seems like everything went well ... */
	}
      else if(buf[0] == 'E')
	{
	  cvs_treat_error(recv, buf);
	  cvs_connection_release(send, recv);
	  return EIO;
	}
      else if(! strncmp(buf, "error", 5))
	{
	  cvs_connection_release(send, recv);
	  return EIO;
	}
      else
	break; /* fuck, what the hell is going on here?? get outta here! */
    }

  /* well, got EOF, that shouldn't ever happen ... */
  cvs_connection_kill(send, recv);
  return EIO;
}



/* cvs_files_hit
 *
 * ask cvs server whether there is a particular revision (as specified by rev)
 * available. return 0 if yes, ENOENT if not. EIO on communication error.
 */
error_t
cvs_files_hit(struct netnode *file, struct revision *rev)
{
  FILE *send, *recv;
  unsigned short int got_something = 0;

  char buf[4096]; /* 4k should be enough for most cvs repositories, if
		   * cvsfs tell's you to increase this value, please do so.
		   */

  if(cvs_connect(&send, &recv))
    return EIO;

  /* write out request header */
  fprintf(send,
	  "UseUnchanged\n"
	  "Argument -s\n"
	  "Argument -r\nArgument 0\n"
	  "Argument -r\nArgument %s\n"
	  "Argument ",
	  rev->id);

  /* write out pathname from rootnode on ... */
  cvs_files_print_path_recursively(send, file->parent);

  /* last but not least write out the filename */
  fprintf(send, "%s\n", file->name);

  /* we need an rdiff ... */
  fprintf(send, "rdiff\n");

  /* okay, now read the server's response, which either is something
   * "M" char padded or an E, error couple.
   */
  while(fgets(buf, sizeof(buf), recv))
    {
      if(! strncmp(buf, "ok", 2))
	{
	  cvs_connection_release(send, recv);

	  if(! got_something)
	    return ENOENT; /* no content, sorry. */

	  return 0; /* jippie, looks perfectly, he? */
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
	  return EIO; /* hm, doesn't look got for us ... */
	}

      switch(buf[0])
	{
	case 'E':
	  /* cvs_treat_error(recv, buf);
	   * cvs_connection_release(send, recv);
	   * return EIO;
	   *
	   * don't call cvs_treat_error since it's probably a
	   * "no such tag %s" message ...
	   */
	  break;

	case 'M':
	  got_something = 1;
	  break;
	  
	default:
	  cvs_treat_error(recv, buf);
	  cvs_connection_release(send, recv);
	  return EIO;
	}
    }

  /* well, got EOF, that shouldn't ever happen ... */
  cvs_connection_kill(send, recv);
  return EIO;
}
