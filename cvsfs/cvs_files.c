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

#define PACKAGE "cvsfs"
#define VERSION "0.1"

#include <string.h>
#include <stdlib.h>
#include <malloc.h>

#include "cvsfs.h"
#include "cvs_files.h"
#include "cvs_connect.h"

/* cvs_files_cache
 *
 * Download the revision (as specified by rev) of the specified file. 
 */
int
cvs_files_cache(struct netnode *file, struct revision *rev)
{
  FILE *send, *recv;
  char *content = NULL;
  unsigned int content_len = 0;
  unsigned int content_alloc = 0;
  unsigned short int got_something = 0;

  char buf[4096]; /* 4k should be enough for most cvs repositories, if
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
	  "UseUnchanged\n"
	  "Argument -r\nArgument 0\n"
	  "Argument -r\nArgument %s\n"
	  "Argument ",
	  rev->id);

  /* now write out the path to our file, recurse up to the root-node and
   * start writing out each directory's name
   */
  void cvs_files_print_path_recursively(FILE *cvs_handle, struct netnode *dir)
    {
      if(dir->parent)
	cvs_files_print_path_recursively(cvs_handle, dir->parent);
      fprintf(cvs_handle, "%s/", dir->name);
    }

  cvs_files_print_path_recursively(send, file->parent);

  /* last but not least write out the filename */
  fprintf(send, "%s\n", file->name);

  /* we need an rdiff ... */
  fprintf(send, "rdiff\n");

  /* okay, now read the server's response, which either is a diff, having
   * a "M" char in front of each line, or an E, error couple.
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

      /* chop the linefeed off the end */
      *ptr = 0;

      if(! strncmp(buf, "ok", 2))
	{
	  if(! got_something)
	    {
	      cvs_connection_release(send, recv);
	      return -1; /* no content, sorry. */
	    }

	  content = realloc(content, content_len + 1);

	  if(! content)
	    {
	      cvs_connection_release(send, recv);
	      return ENOMEM;
	    }

	  content[content_len] = 0; /* zero terminate */
	  rev->contents = content;
	  
	  cvs_connection_release(send, recv);
	  return 0; /* jippie, looks perfectly, he? */
	}

      if(! strncmp(buf, "error", 5))
	{
	  cvs_connection_release(send, recv);
	  return -1; /* wicked, get outta here anyhow ... 
		      * TODO care for memory, that might got allocated already
		      */
	}

      if(buf[1] != ' ') 
	{
	  cvs_treat_error(recv, buf);
	  cvs_connection_release(send, recv);
	  return -1; /* hm, doesn't look got for us
		      * anyhow, we need to care for allocated memory here! TODO
		      */
	}

      switch(buf[0])
	{
	case 'E': /* we expect a patch, padded by M's, this is not what
		   * we want to have, keep on complaining 
		   */
	  cvs_treat_error(recv, buf);
	  cvs_connection_release(send, recv);
	  return -1; /* TODO free memory */

	case 'M':
	  got_something = 1;

	  if(buf[2] != '+')
	    continue; /* skip patch's header, we don't need it ... */

	  while(content_len + buflen - 4 > content_alloc) 
	    {
	      content_alloc = content_alloc ? content_alloc << 2 : 4096;
	      content = realloc(content, content_alloc);
	      
	      if(! content)
		exit(11); /* FIXME make cvsfs survive little longer ... */
	    }

	  memcpy(content + content_len, buf + 4, buflen - 5);
	  content_len += buflen - 5;

	  content[content_len ++] = 10; /* linefeed */
	  break;
	  
	default:
	  cvs_treat_error(recv, buf);
	  cvs_connection_release(send, recv);
	  return -1; /* TODO this probably never happens, but care
		      * for allocated memory anyways
		      */
	}
    }
  
  cvs_connection_release(send, recv);
  return -1; /* got eof, this shouldn't happen. 
	      * free allocated memory anyway. */      
}
