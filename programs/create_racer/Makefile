TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = create_racer

CC = $(MPICC)

CFLAGS = -O2 -Wall -g $(O2DLM_CFLAGS) $(OCFS2_CFLAGS)

SOURCES = create_racer.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

lvb_torture: $(OBJECTS)
	$(LINK)

include $(TOPDIR)/Test.make
include $(TOPDIR)/Postamble.make
