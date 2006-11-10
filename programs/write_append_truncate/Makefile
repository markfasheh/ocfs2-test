TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = write_append_truncate

CC = $(MPICC)

CFLAGS = -O2 -Wall -g

SOURCES = write_append_truncate.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = write_append_truncate

write_append_truncate: $(OBJECTS)
	$(LINK)

include $(TOPDIR)/Postamble.make
