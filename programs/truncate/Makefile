TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = truncate

CFLAGS = -O2 -Wall -g

SOURCES = truncate.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

truncate: $(OBJECTS)
	$(LINK) 

include $(TOPDIR)/Test.make
include $(TOPDIR)/Postamble.make
