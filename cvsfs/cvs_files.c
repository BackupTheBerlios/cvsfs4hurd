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
#include <fcntl.h>

#ifdef HAVE_LIBZ
#  include <zlib.h>
#endif

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


/* gzip flag byte */
#define ASCII_FLAG   0x01 /* bit 0 set: file probably ascii text */
#define HEAD_CRC     0x02 /* bit 1 set: header CRC present */
#define EXTRA_FIELD  0x04 /* bit 2 set: extra field present */
#define ORIG_NAME    0x08 /* bit 3 set: original file name present */
#define COMMENT      0x10 /* bit 4 set: file comment present */
#define RESERVED     0xE0 /* bits 5..7: reserved */

/* cvs_files_gzip_check_header
 *
 * check the gzip header for validity and move the pointers as necessary.
 * RETURN: 0 on success, 1 if not enough bytes we available or a system error
 * code on trouble
 */
#ifdef HAVE_LIBZ
static int
cvs_files_gzip_check_header(char **data, int *len)
{
  static const char gzip_magic[2] = {0x1f, 0x8b};
  int pos = 10;
  int method;
  int flags;

  if(*len < 10) 
    return 1; /* not enough bytes for the header */

  if((*data)[0] != gzip_magic[0] || (*data)[1] != gzip_magic[1])
    return EIO; /* not a gzip magic */

  method = (*data)[2];
  flags = (*data)[3];
  if(method != Z_DEFLATED || (flags & RESERVED))
    return EIO; /* not deflated or reserved flags set, what about it?? */

  /* bytes 4..9 are time, xflags and os type. ignore 'em ... */

  if(flags & EXTRA_FIELD)
    {
      int field_len;

      if(*len < pos + 2) return 1; /* not enough data */

      field_len = (*data)[pos ++];
      field_len = (*data)[pos ++];

      if(*len < pos + field_len) return 1; /* not enough data
					    * TODO if extra_field is larger
					    * than our gzip in buffer, we
					    * got a problem ...
					    */
      pos =+ field_len;
    }
  
  if(flags & ORIG_NAME)
    do
      /* skip the original file name ... */
      if(*len < pos + 1) return 1; /* not enough data */
    while((*data)[pos ++]);

  if(flags & COMMENT)
    do
      /* skip the comment ... */
      if(*len < pos + 1) return 1; /* not enough data */
    while((*data)[pos ++]);

  if(flags & HEAD_CRC)
    {
      /* skip two bytes, i.e. header's crc */
      if(*len < pos + 2) return 1; /* not available */
      pos += 2;
    }

  /* okay, we got it, therefore adjust *len and *data accordingly */
  *len -= pos;
  *data += pos;
  return 0;
}
#endif /* HAVE_LIBZ */



/* cvs_files_gzip_inflate
 *
 * gzip inflate given number of bytes from file-handle into content
 * return 0 on success.
 */
#if HAVE_LIBZ
static error_t
cvs_files_gzip_inflate(FILE *recv, size_t bytes, char **content, size_t *len)
{
  char input[4096]; /* input buffer */
  size_t read;
  z_stream z;
  int got_header = 0;
  
  /* initialize zlib decompression */
  z.next_in = input;
  z.avail_in = 0;
  z.next_out = NULL;
  z.zalloc = Z_NULL;
  z.zfree = Z_NULL;
  z.opaque = Z_NULL;

  switch(inflateInit2(&z, -MAX_WBITS))
    {
    case Z_OK:
      break; /* it worked, perfect. */

    case Z_MEM_ERROR:
      return ENOMEM;

    case Z_VERSION_ERROR:
      fprintf(stderr, PACKAGE ": incompatible zlib installed.\n");
      return EIEIO;

    default:
      return EIO;
    }

  /* read 'bytes' bytes from reader handle, but not more than we can store
   * to input buffer ...
   */
  while(bytes && (read = sizeof(input) -
		  ((char*)z.next_in - input) - z.avail_in)
	&& (read = fread(z.next_in + z.avail_in, 1,
			 read < bytes ? read : bytes, recv)))
    {
      bytes -= read;
      z.avail_in += read;

      /* check whether there is a correct gzip header */
      if(! got_header)
	switch(cvs_files_gzip_check_header((char **)&z.next_in, &z.avail_in))
	  {
	  case 0: /* success */
	    got_header = 1;
	    break;

	  case 1: /* not enough data */
	    continue;

	  default:
	    inflateEnd(&z);

	    if(z.next_out)
	      free(z.next_out - z.total_out);

	    return EIO;
	  }	  

      /* enlare output buffer, if necessary */
      if(! z.next_out || z.avail_out < (z.avail_in << 2))
	{
	  void *ptr;
	  size_t alloc = z.next_out ? z.total_out << 1 : 16384;

	  ptr = realloc(z.next_out - z.total_out, alloc);
	  if(! ptr)
	    {
	      inflateEnd(&z);
	      return ENOMEM;
	    }

	  z.next_out = ptr + z.total_out;
	  z.avail_out = alloc - z.total_out;
	}

      /* inflate data now */
      switch(inflate(&z, Z_NO_FLUSH))
	{
	case Z_OK:
	case Z_STREAM_END:
	case Z_BUF_ERROR: /* this is not fatal, just not enough
			   * memory in output buffer
			   */
	  break;

	case Z_NEED_DICT:
	case Z_DATA_ERROR:
	case Z_STREAM_ERROR:
	  inflateEnd(&z);

	  if(z.next_out)
	    free(z.next_out - z.total_out);

	  return EIO;
		  
	case Z_MEM_ERROR:
	default:
	  inflateEnd(&z);

	  if(z.next_out)
	    free(z.next_out - z.total_out);

	  return ENOMEM;
	}

      /* discard inflated bits from the input buffer ... */
      memmove(input, z.next_in, sizeof(input) - ((char *)z.next_in - input));
      z.next_in = input;
    }

  if(bytes)
    {
      /* unable to read all data ... */
      inflateEnd(&z);

      if(z.next_out)
	free(z.next_out - z.total_out);

      return EIO;
    }

  while(z.avail_in)
    {
      /* data left in input buffer, call inflate to care for that */
      void *ptr;
      size_t alloc = z.total_out + (z.avail_in << 2);

      ptr = realloc(z.next_out - z.total_out, alloc);
      if(! ptr)
	{
	  inflateEnd(&z);
	  return ENOMEM;
	}

      z.next_out = ptr + z.total_out;
      z.avail_out = alloc - z.total_out;

      switch(inflate(&z, Z_FINISH))
	{
	case Z_STREAM_END:
	  z.avail_in = 0; /* okay, we're done, forget the trailing bits */

	case Z_OK:
	case Z_BUF_ERROR: /* this is not fatal, just not enough
			   * memory in output buffer
			   */
	  break;
	  
	case Z_NEED_DICT:
	case Z_DATA_ERROR:
	case Z_STREAM_ERROR:
	  inflateEnd(&z);

	  if(z.next_out)
	    free(z.next_out - z.total_out);

	  return EIO;
		  
	case Z_MEM_ERROR:
	default:
	  inflateEnd(&z);

	  if(z.next_out)
	    free(z.next_out - z.total_out);

	  return ENOMEM;
	}
    }

  inflateEnd(&z);

  /* okay, processed data should be at z.next_out - z.total_out */
  free(*content);
  *content = realloc(z.next_out - z.total_out, z.total_out);
  *len = z.total_out;

  return 0;
}
#endif /* HAVE_LIBZ */



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

#if HAVE_LIBZ
      else if(buf[0] == 'z')
	{
	  /* okay, we'll see a gzipped data stream 
	   * the number tells us the number of bytes we will retrieve,
	   * not the size of the inflated file!
	   */
	  size_t bytes = atoi(buf + 1);
	  error_t err;
	  
	  if(! bytes)
	    {
	      cvs_connection_kill(send, recv);
	      return EIO; /* this should not happen, empty file?? */
	    }

	  if((err = cvs_files_gzip_inflate(recv, bytes,
					   &rev->contents, &rev->length)))
	    {
	      cvs_connection_kill(send, recv);
	      return err;
	    }
	}
#endif /* HAVE_LIBZ */

      else if(buf[0] >= '0' && buf[0] <= '9')
	{
	  /* okay, this tells the length of our file.
	   * the file is transmitted without compression applied
	   */
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
