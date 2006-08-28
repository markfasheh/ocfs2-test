TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = create_and_open

CC = $(MPICC)

CFLAGS = -O2 -Wall -g $(O2DLM_CFLAGS) $(OCFS2_CFLAGS)

SOURCES = create_and_open.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

create_and_open: $(OBJECTS)
	$(LINK)

include $(TOPDIR)/Test.make
include $(TOPDIR)/Postamble.make
