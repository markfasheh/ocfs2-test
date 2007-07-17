TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = forkwriter

#CFLAGS = -O2 -Wall -g -D_GNU_SOURCE
CFLAGS = -O -Wall -g -D_GNU_SOURCE

FORKWRITER_SOURCES = forkwriter.c
FORKWRITER_OBJECTS = $(patsubst %.c,%.o,$(FORKWRITER_SOURCES))

SOURCES = $(FORKWRITER_SOURCES)

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = forkwriter

#BIN_EXTRA = 

forkwriter: $(FORKWRITER_OBJECTS)
	$(LINK) 

include $(TOPDIR)/Postamble.make
