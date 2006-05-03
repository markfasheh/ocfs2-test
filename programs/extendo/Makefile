TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = extendo

CFLAGS = -O2 -Wall -g

SOURCES = extendo.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

extendo: $(OBJECTS)
	$(LINK) 

include $(TOPDIR)/Test.make
include $(TOPDIR)/Postamble.make
