TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = truncate_direct

CLEAN_FILES = $(TESTS)

CFLAGS = -O2 -Wall -g

SOURCES = truncate_direct.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = $(TESTS)

truncate_direct: $(OBJECTS)
	$(LINK) 

include $(TOPDIR)/Postamble.make
