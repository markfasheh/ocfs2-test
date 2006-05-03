TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = dommap

CFLAGS = -O2 -Wall -g

SOURCES = dommap.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

dommap: $(OBJECTS)
	$(LINK) 

include $(TOPDIR)/Test.make
include $(TOPDIR)/Postamble.make
