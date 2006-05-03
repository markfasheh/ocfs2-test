TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = mmap_test

CFLAGS = -O2 -Wall -g

SOURCES = mmap_test.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

mmap_test: $(OBJECTS)
	$(LINK) 

include $(TOPDIR)/Test.make
include $(TOPDIR)/Postamble.make
