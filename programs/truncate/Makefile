TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = truncate

CFLAGS = -O2 -Wall -g

SOURCES = truncate.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

SUBDIRS = ocfs2-tools

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = truncate

truncate: $(OBJECTS)
	$(LINK) 

include $(TOPDIR)/Postamble.make
