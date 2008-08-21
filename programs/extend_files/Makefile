TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = truncate_direct

CLEAN_FILES = $(TESTS)

CFLAGS = -O2 -Wall -g

SOURCES = truncate_direct.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = $(TESTS)

BIN_EXTRA = extend_files.sh

truncate_direct: $(OBJECTS)
	$(LINK) 

include $(TOPDIR)/Postamble.make
