TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = inline-data inline-dirs multi-inline-data multi-inline-dirs

CFLAGS = -O2 -Wall -g

MPI_LINK = $(MPICC) $(CFLAGS) $(LDFLAGS) -o $@ $^

SOURCES = inline-data.c inline-dirs.c inline-data-utils.c inline-dirs-utils.c multi-inline-data.c multi-inline-dirs.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = inline-data inline-dirs multi-inline-data multi-inline-dirs

BIN_EXTRA =  single-inline-run.sh multi-inline-run.sh

inline-data: inline-data.o inline-data-utils.o
	$(LINK) -locfs2

inline-dirs: inline-dirs.o inline-dirs-utils.o
	$(LINK) -locfs2

multi-inline-data.o: multi-inline-data.c
	$(MPICC) -c multi-inline-data.c

multi-inline-dirs.o: multi-inline-dirs.c
	$(MPICC) -c multi-inline-dirs.c

multi-inline-data: multi-inline-data.o inline-data-utils.o
	$(MPI_LINK) -locfs2

multi-inline-dirs: multi-inline-dirs.o
	$(MPI_LINK) -locfs2

include $(TOPDIR)/Postamble.make
