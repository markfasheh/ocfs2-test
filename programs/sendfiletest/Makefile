TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = sendfiletest

CC = $(MPICC)

CFLAGS = -O2 -Wall -g $(O2DLM_CFLAGS) $(OCFS2_CFLAGS)

SOURCES = sendfiletest.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = sendfiletest

sendfiletest: $(OBJECTS)
	$(LINK)

include $(TOPDIR)/Postamble.make
