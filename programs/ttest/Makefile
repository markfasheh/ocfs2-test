TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = ttest

CFLAGS = -O2 -Wall -g

SOURCES = ttest.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = ttest

ttest: $(OBJECTS)
	$(LINK) 

include $(TOPDIR)/Postamble.make
