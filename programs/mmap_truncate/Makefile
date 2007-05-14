TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = mmap_truncate

CFLAGS = -O2 -Wall -g $(OCFS2_CFLAGS)

SOURCES = mmap_truncate.c

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = mmap_truncate

mmap_truncate: mmap_truncate.o
	$(LINK) $(OCFS2_LIBS)

include $(TOPDIR)/Postamble.make
