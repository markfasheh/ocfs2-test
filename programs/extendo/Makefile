TOPDIR = ../..

include $(TOPDIR)/Preamble.make
include $(TOPDIR)/Test.make

TESTS = extendo

CFLAGS = -O2 -Wall -g

SOURCES = extendo.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

extendo: $(OBJECTS)
	$(LINK) 

include $(TOPDIR)/Postamble.make
