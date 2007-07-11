TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = reserve_space

CFLAGS = -O2 -Wall -g

SOURCES = reserve_space.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = reserve_space

reserve_space: $(OBJECTS)
	$(LINK) 

include $(TOPDIR)/Postamble.make
