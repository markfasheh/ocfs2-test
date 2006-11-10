TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = lock_grab

CFLAGS = -O2 -Wall -g $(O2DLM_CFLAGS)

SOURCES = lock_grab.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = lock_grab

lock_grab: $(OBJECTS)
	$(LINK) $(O2DLM_LIBS)

include $(TOPDIR)/Postamble.make
