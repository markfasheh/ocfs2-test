TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = dx_dirs_tests

CFLAGS = -O2 -Wall -g $(OCFS2_CFLAGS)

MPI_LINK = $(MPICC) $(CFLAGS) $(LDFLAGS) -o $@ $^

SOURCES = index_dir.c multi_index_dir.c

DIST_FILES = $(SOURCES) index_dir_run.sh multi_index_dir_run.sh

BIN_EXTRA = index_dir_run.sh multi_index_dir_run.sh

BIN_PROGRAMS = index_dir multi_index_dir

index_dir: index_dir.c
	$(LINK) $(OCFS2_LIBS)

multi_index_dir: multi_index_dir.c
	$(MPI_LINK) $(OCFS2_LIBS)

include $(TOPDIR)/Postamble.make
