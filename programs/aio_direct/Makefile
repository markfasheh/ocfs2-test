TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = partial_aio_direct

CFLAGS = -O2 -Wall -g $(O2DLM_CFLAGS) $(OCFS2_CFLAGS)

SOURCES = partial_aio_direct.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = partial_aio_direct

partial_aio_direct: $(OBJECTS)
	$(LINK) -laio

include $(TOPDIR)/Postamble.make
