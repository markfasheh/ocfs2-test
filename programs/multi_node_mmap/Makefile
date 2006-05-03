TOPDIR = ../..

include $(TOPDIR)/Preamble.make
include $(TOPDIR)/Test.make

TESTS = dommap

CFLAGS = -O2 -Wall -g

SOURCES = dommap.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

dommap: $(OBJECTS)
	$(LINK) 

include $(TOPDIR)/Postamble.make
