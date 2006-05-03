TOPDIR = ../..

include $(TOPDIR)/Preamble.make
include $(TOPDIR)/Test.make

TESTS = write_torture

CFLAGS = -O2 -Wall -g

SOURCES = write_torture.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

write_torture: $(OBJECTS)
	$(LINK) 

include $(TOPDIR)/Postamble.make
