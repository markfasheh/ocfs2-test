TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = truncate verify_truncate

CFLAGS = -O2 -Wall -g $(OCFS2_CFLAGS)

SOURCES = truncate.c verify_truncate.c

SUBDIRS = ocfs2-tools

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = truncate verify_truncate

truncate: truncate.o
	$(LINK)

verify_truncate: verify_truncate.o
	$(LINK) $(OCFS2_LIBS)

include $(TOPDIR)/Postamble.make
