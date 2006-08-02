TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = aio-stress

CFLAGS = -O2 -Wall -g
LDFLAGS += -laio -lpthread

SOURCES = aio-stress.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)
  
truncate: $(OBJECTS)
	$(LINK)

include $(TOPDIR)/Test.make
include $(TOPDIR)/Postamble.make
