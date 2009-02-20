TOPDIR = ../..

include $(TOPDIR)/Preamble.make

CFLAGS = -O2 -Wall -g

SOURCES = fsx-linux.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = fsx

BIN_EXTRA = fsx-run.sh

fsx: $(OBJECTS)
	$(LINK) 

include $(TOPDIR)/Postamble.make
