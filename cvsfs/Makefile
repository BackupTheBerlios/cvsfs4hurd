###########################################################
# Makefile
#
# Copyright 2004, Stefan Siegl <ssiegl@gmx.de>, Germany
# 
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Publice License,
# version 2 or any later. The license is contained in the COPYING
# file that comes with the cvsfs4hurd distribution.
#

CFLAGS+=-Wall -ggdb
CFLAGS+=-std=gnu99 -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64
LDFLAGS+=-Wall -W -ggdb
LDFLAGS+=-lnetfs -lfshelp -liohelp -lthreads -lports
OBJS=tcpip.o cvsfs.o cvs_connect.o cvs_pserver.o cvs_tree.o netfs.o \
	node.o cvs_files.o cvs_ext.o

all: cvsfs

cvsfs: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

.c.o:
	$(CC) -I. $(CFLAGS) -c $<

clean:
	rm -f *.[oda] core tags cvsfs

%.d: %.c
	$(CC) -MM -I. $(CFLAGS) $< >.$@.dep
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < .$@.dep > $@
	rm -f .$@.dep

-include $(OBJS:.o=.d)
