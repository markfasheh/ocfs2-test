TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = discontig_bg_test

CFLAGS = -O2 -Wall -g $(OCFS2_CFLAGS)

MPI_LINK = $(MPICC) $(CFLAGS) $(LDFLAGS) -o $@ $^

SOURCES = spawn_inodes.c

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = spawn_inodes

spawn_inode: spawn_inodes.c
	$(LINK) $(OCFS2_LIBS)

include $(TOPDIR)/Postamble.make
