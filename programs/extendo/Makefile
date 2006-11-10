TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = extendo

CFLAGS = -O2 -Wall -g

SOURCES = extendo.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = extendo

extendo: $(OBJECTS)
	$(LINK) 

include $(TOPDIR)/Postamble.make
