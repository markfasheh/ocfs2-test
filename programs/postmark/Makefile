TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = postmark-1_5

CFLAGS = -O2 -Wall -g

SOURCES = postmark-1_5.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = postmark-1_5

postmark-1_5: $(OBJECTS)
	$(LINK)

include $(TOPDIR)/Postamble.make
