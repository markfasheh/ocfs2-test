TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = logwriter

CFLAGS = -O2 -Wall -g

SOURCES = logwriter.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = logwriter

logwriter: $(OBJECTS)
	$(LINK) 

include $(TOPDIR)/Postamble.make
