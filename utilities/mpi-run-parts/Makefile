TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = mpi-run-parts

CC = $(MPICC)

CFLAGS = -O2 -Wall -g

SOURCES = mpi-run-parts.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

mpi-run-parts: $(OBJECTS)
	$(LINK)

include $(TOPDIR)/Test.make
include $(TOPDIR)/Postamble.make
