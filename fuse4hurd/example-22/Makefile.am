## Process this file with automake to produce Makefile.in

noinst_PROGRAMS = fusexmp null hello

fusexmp_SOURCES = fusexmp.c
null_SOURCES = null.c
hello_SOURCES = hello.c

LDADD = ../libfuse.la
CFLAGS = @CFLAGS@ -DFUSE_USE_VERSION=22

