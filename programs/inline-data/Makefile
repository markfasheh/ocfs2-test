TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = inline-data inline-dirs

CFLAGS = -O2 -Wall -g

SOURCES = inline-data.c inline-dirs.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = inline-data inline-dirs

inline-data: inline-data.o
	$(LINK)

inline-dirs: inline-dirs.o
	$(LINK)

include $(TOPDIR)/Postamble.make
