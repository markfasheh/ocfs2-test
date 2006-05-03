TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = lvb_torture

CC = $(MPICC)

CFLAGS = -O2 -Wall -g $(O2DLM_CFLAGS) $(OCFS2_CFLAGS)

SOURCES = lvb_torture.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

lvb_torture: $(OBJECTS)
	$(LINK) $(O2DLM_LIBS)

include $(TOPDIR)/Test.make
include $(TOPDIR)/Postamble.make
