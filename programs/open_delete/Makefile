TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = open_delete

CC = $(MPICC)

CFLAGS = -O2 -Wall -g $(O2DLM_CFLAGS) $(OCFS2_CFLAGS)

SOURCES = open_delete.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES) open_delete.py

BIN_EXTRA = open_delete.py

BIN_PROGRAMS = open_delete

open_delete: $(OBJECTS)
	$(LINK) $(O2DLM_LIBS)

include $(TOPDIR)/Postamble.make
