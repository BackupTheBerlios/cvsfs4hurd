#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
#
# Copyright(C) 2004 by Stefan Siegl <ssiegl@gmx.de>, Germany

AC_PREREQ(2.59)
AC_INIT(cvsfs4hurd, 0.1, cvsfs4hurd-devel@lists.berlios.de)
AC_REVISION($Revision: 1.2 $)
AC_CONFIG_SRCDIR([cvs_files.c])
AM_CONFIG_HEADER(config.h)

# Initialize automake 
AM_INIT_AUTOMAKE(cvsfs4hurd, 0.1)

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
AC_CHECK_LIB([threads], [rwlock_init])
AC_CHECK_LIB([ports], [hurd_ihash_init])
AC_CHECK_LIB([fshelp], [fshelp_touch])
AC_CHECK_LIB([iohelp], [iohelp_initialize_conch])
#AC_CHECK_LIB([netfs], [netfs_startup])

# Check whether zlib compression is available
AC_CHECK_LIB(z, gzopen, ,[
    echo "*** zlib development library was not found ***"
    echo "cannot use gzip compressed file download."
    echo ""
   ])

LIBS="-lnetfs $LIBS"
CFLAGS="-D_GNU_SOURCE -D_FILE_OFFSET_BITS=64"

AC_OUTPUT([Makefile])
