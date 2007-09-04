TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = create_racer

CC = $(MPICC)

CFLAGS = -O2 -Wall -g $(O2DLM_CFLAGS) $(OCFS2_CFLAGS)

SOURCES = create_racer.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES) run_create_racer.py

BIN_PROGRAMS = create_racer

BIN_EXTRA = run_create_racer.py

lvb_torture: $(OBJECTS)
	$(LINK)

include $(TOPDIR)/Postamble.make
