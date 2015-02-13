TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = aio-stress
BIN_PROGRAMS = aio-stress

CFLAGS = -O2 -Wall -g
LDFLAGS += -laio -lpthread

SOURCES = aio-stress.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)
  
aio-stress: $(OBJECTS)
	$(LINK) -laio -lpthread

include $(TOPDIR)/Postamble.make
