/**********************************************************
 * cvsfs.c
 *
 * Copyright (C) 2004, 2005 by Stefan Siegl <ssiegl@gmx.de>, Germany
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Publice License,
 * version 2 or any later. The license is contained in the COPYING
 * file that comes with the cvsfs4hurd distribution.
 *
 * translator startup code (netfs startup & argp handling)
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <argp.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <error.h>
#include <netdb.h>
#include <sys/stat.h>
#include <hurd/netfs.h>

#include "cvsfs.h"
#include "cvs_connect.h"
#include "cvs_tree.h"
#include "node.h"

/* cvsfs configuration: cvs pserver hostname, rootdir, modulename, etc. */
cvsfs_config config;

/* cvsfs entry stat template */
cvsfs_stat_template stat_template;

/* global variables, needed for netfs */
char *netfs_server_name = PACKAGE;
char *netfs_server_version = VERSION;
int netfs_maxsymlinks = 12;



static error_t parse_cvsfs_opt(int key, char *arg, struct argp_state *state);

static const struct argp_child argp_children[] =
  {
    {&netfs_std_startup_argp, 0, NULL, 0}, 
    {NULL, 0, NULL, 0} 
  };



/* documentation, written out when called with either --usage or --help */
const char *argp_program_version = "cvsfs (" PACKAGE ") " VERSION "\n"
"Written by Stefan Siegl\n\n"
"Copyright (C) 2004, 2005 Stefan Siegl <ssiegl@gmx.de>, Germany\n"
"This is free software; see the source for copying conditions.  There is NO\n"
"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE."
"\n";
static char *args_doc = "HOSTNAME CVSROOT MODULE";
static char *doc = "cvs filesystem translator for the Hurd.\v"
"Please mind that " PACKAGE " is currently very much in alpha alike state, "
"therefore please do not expect a translator working perfectly right now.\n\n"
PACKAGE " translator will show the remotely located CVS module 'MODULE' "
"from host 'HOSTNAME' as it's files were located right on your computer.";

/* options our translator understands, to be used by libc argp */
enum 
  {
    OPT_PORT = 'p',
    OPT_USER = 'u',
    OPT_HOMEDIR = 'h',
    OPT_REMOTE = 'r',
    OPT_LOCAL = 'l',
    OPT_NOSTATS = 'n',
    OPT_DEBUG = 'd',
#ifdef HAVE_LIBZ
    OPT_GZIP = 'z',
#endif
  };

static const struct argp_option cvsfs_options[] =
  { 
    { "port", OPT_PORT, "PORT", 0,
      "port to connect to on given host (if not using standard port)", 0 },
    { "user", OPT_USER, "USERNAME", 0,
      "username to supply to cvs host, when logging in", 0 },
    { "homedir", OPT_HOMEDIR, "PATH", 0,
      "path of your home directory (= path to .cvspass file)", 0 },
    { "remote", OPT_REMOTE, "CLIENT", OPTION_ARG_OPTIONAL,
      "connect through :ext: remote shell client CLIENT to cvs host", 0 },
    { "local", OPT_LOCAL, 0, 0,
      "show files from local cvs repository", 0 },
    { "nostats", OPT_NOSTATS, 0, 0,
      "do not download revisions to aquire stats information", 0 },
    { "debug", OPT_DEBUG, "FILE", OPTION_ARG_OPTIONAL,
      "print debug output to FILE or stderr", 0 },
#if HAVE_LIBZ
    { "gzip", OPT_GZIP, "LEVEL", 0,
      "use gzip compression of specified level for file transfers", 0 },
#endif
    /* terminate list */
    { NULL, 0, NULL, 0, NULL, 0 }
  };

volatile struct mapped_time_value *cvsfs_maptime;

/* pointer to root netnode */
struct netnode *rootdir = NULL;


int
main(int argc, char **argv)
{
  io_statbuf_t ul_stat;
  mach_port_t bootstrap, ul_node;
  struct argp argp =
    {
      cvsfs_options, parse_cvsfs_opt, 
      args_doc, doc, argp_children, 
      NULL, NULL
    };

  cvs_connect_init();

  /* reset configuration structure to sane defaults */
  memset(&config, 0, sizeof(config));
  config.cvs_mode = PSERVER;
  config.cvs_username = "anonymous";
  config.debug_port = NULL; /* no debugging by default */
#if HAVE_LIBZ
  config.gzip_level = 3;
#endif

  /* parse command line parameters, first things first. */
  argp_parse(&argp, argc, argv, 0, 0, 0);

  /* set up our translator now ... */
  task_get_bootstrap_port(mach_task_self(), &bootstrap);
  netfs_init();

  /* start up netfs stuff */
  ul_node = netfs_startup(bootstrap, 0);
  
  if(io_stat(ul_node, &ul_stat))
    {
      perror(PACKAGE ": cannot stat underlying node");
      return 1;
    }

  /* initialize our stat_template structure */
  stat_template.uid = ul_stat.st_uid;
  stat_template.gid = ul_stat.st_gid;
  stat_template.author = ul_stat.st_author;
  stat_template.fsid = getpid();
  stat_template.mode = ul_stat.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);

  /* download initial root directory */
  if(cvs_tree_read(&rootdir))
    {
      fprintf(stderr, PACKAGE ": unable to get initial cvs tree, stop.\n");
      return 1;
    }

  /* map time */
  if(maptime_map(0, 0, &cvsfs_maptime))
    {
      perror(PACKAGE ": cannot map time");
      return 1;
    }

  /* create our root node */
  netfs_root_node = cvsfs_make_node(rootdir);
  if(! netfs_root_node)
    {
      perror(PACKAGE ": cannot create rootnode");
      return 1;
    }

  netfs_server_loop();
  return 1; /* netfs_server_loop doesn't return, exit with status 1, if
	     * it returns anyways ...
	     */
}
    

/* parse_cvsfs_opt
 *
 * argp parser function for cvsfs'es command line args 
 */  
static error_t 
parse_cvsfs_opt(int key, char *arg, struct argp_state *state)
{
  switch(key) 
    {
    case OPT_PORT:
      config.cvs_port = atoi(arg);
      break;

    case OPT_USER:
      config.cvs_username = strdup(arg);
      break;

    case OPT_HOMEDIR:
      config.homedir = strdup(arg);
      break;

    case OPT_NOSTATS:
      config.nostats = 1;
      break;

    case OPT_DEBUG:
      if(arg)
	{
	  config.debug_port = fopen(arg, "w");
	  if(errno)
	    perror(PACKAGE);
	}

      /* if either no file was specified or in case we cannot open it,
       * write debugging output to standard error */
      if(! config.debug_port)
	config.debug_port = stderr;

      break;

#if HAVE_LIBZ
    case OPT_GZIP:
      config.gzip_level = atoi(arg);
      break;
#endif

    case OPT_REMOTE:
      config.cvs_mode = EXT;
      if(arg)
	config.cvs_shell_client = strdup(arg);
      else
	{
	  const char *rsh = getenv("CVS_RSH");
	  if(rsh)
	    config.cvs_shell_client = strdup(arg);
	  else
	    config.cvs_shell_client = "rsh";
	}
      break;

    case OPT_LOCAL:
      config.cvs_mode = LOCAL;
      break;

    case ARGP_KEY_ARGS:
      if(state->argc - state->next != 3)
	argp_usage(state);
      else
	{
	  if(strcmp(state->argv[state->next], "localhost"))
	    config.cvs_hostname = strdup(state->argv[state->next]);
	  else
	    config.cvs_mode = LOCAL;

	  state->next ++;
	  config.cvs_root = strdup(state->argv[state->next ++]);
	  config.cvs_module = strdup(state->argv[state->next ++]);
	}
      break;

    case ARGP_KEY_NO_ARGS:
      argp_usage(state);
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }

  return 0;
}
