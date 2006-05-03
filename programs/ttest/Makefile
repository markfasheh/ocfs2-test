TOPDIR = ../..

include $(TOPDIR)/Preamble.make
include $(TOPDIR)/Test.make

TESTS = ttest

CFLAGS = -O2 -Wall -g

SOURCES = ttest.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

ttest: $(OBJECTS)
	$(LINK) 

include $(TOPDIR)/Postamble.make
