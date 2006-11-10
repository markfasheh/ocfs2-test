TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = multi_mmap

CC = $(MPICC)

CFLAGS = -O2 -Wall -g

SOURCES = multi_mmap.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = multi_mmap

multi_mmap: $(OBJECTS)
	$(LINK) 

include $(TOPDIR)/Postamble.make
