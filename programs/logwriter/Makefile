TOPDIR = ../..

include $(TOPDIR)/Preamble.make
include $(TOPDIR)/Test.make

TESTS = logwriter

CFLAGS = -O2 -Wall -g

SOURCES = logwriter.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

logwriter: $(OBJECTS)
	$(LINK) 

include $(TOPDIR)/Postamble.make
