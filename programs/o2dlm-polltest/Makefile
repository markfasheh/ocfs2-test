TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = o2dlm-polltest

CC = $(MPICC)

CFLAGS = -O2 -Wall -g $(O2DLM_CFLAGS) $(OCFS2_CFLAGS)

SOURCES = o2dlm-polltest.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = o2dlm-polltest

o2dlm-polltest: $(OBJECTS)
	$(LINK) $(O2DLM_LIBS)

include $(TOPDIR)/Postamble.make
