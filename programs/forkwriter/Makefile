TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = forkwriter

#CFLAGS = -O2 -Wall -g -D_GNU_SOURCE
CFLAGS = -O -Wall -g -D_GNU_SOURCE

FORKWRITER_SOURCES = forkwriter.c
FORKWRITER_OBJECTS = $(patsubst %.c,%.o,$(FORKWRITER_SOURCES))

SOURCES = $(FORKWRITER_SOURCES)
OBJECTS = $(FORKWRITER_OBJECTS)

DIST_FILES = $(SOURCES) run_forkwriter.py

BIN_PROGRAMS = forkwriter

BIN_EXTRA = run_forkwriter.py 

forkwriter: $(FORKWRITER_OBJECTS)
	$(LINK) 

include $(TOPDIR)/Postamble.make
