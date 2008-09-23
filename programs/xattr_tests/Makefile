TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = xattr-test xattr-multi-test

CFLAGS = -O2 -Wall -g

MPI_LINK = $(MPICC) $(CFLAGS) $(LDFLAGS) -o $@ $^

SOURCES = xattr-test.c xattr-test-utils.c xattr-multi-test.c xattr-test.h
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = xattr-test xattr-multi-test

BIN_EXTRA = xattr-single-run.sh xattr-multi-run.sh

xattr-test: xattr-test.o xattr-test-utils.o xattr-test.h
	$(LINK)

xattr-multi-test: xattr-multi-test.o xattr-test-utils.o xattr-test.h
	$(MPI_LINK)

xattr-multi-test.o: xattr-multi-test.c
	$(MPICC) -c xattr-multi-test.c

include $(TOPDIR)/Postamble.make
