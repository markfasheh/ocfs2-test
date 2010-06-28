TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = discontig_bg_test

CFLAGS = -O2 -Wall -g $(OCFS2_CFLAGS)

MPI_LINK = $(MPICC) $(CFLAGS) $(LDFLAGS) -o $@ $^

SOURCES = spawn_inodes.c gen_extents.c resv.h resv_unwritten.c

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = spawn_inodes gen_extents resv_unwritten

spawn_inode: spawn_inodes.c
	$(LINK) $(OCFS2_LIBS)

gen_extents: gen_extents.c
	$(LINK) $(OCFS2_LIBS)

resv_unwritten: resv_unwritten.c
	$(LINK) $(OCFS2_LIBS)

include $(TOPDIR)/Postamble.make
