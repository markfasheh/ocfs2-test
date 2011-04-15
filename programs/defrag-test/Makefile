TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = defrag-test

CFLAGS = -O2 -Wall -g $(OCFS2_CFLAGS)

CFLAGS += $(EXTRA_CFLAGS)

INCLUDES = -I$(TOPDIR)/programs/libocfs2test

CFLAGS += $(INCLUDES)

LIBO2TEST = $(TOPDIR)/programs/libocfs2test/libocfs2test.a

SOURCES = frager.c

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = frager

frager: frager.o
	$(LINK) $(OCFS2_LIBS) $(LIBO2TEST)

include $(TOPDIR)/Postamble.make
