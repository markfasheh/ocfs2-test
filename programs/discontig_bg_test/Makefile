TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = discontig_bg_test

CFLAGS = -O2 -Wall -g $(OCFS2_CFLAGS)

MPI_LINK = $(MPICC) $(CFLAGS) $(LDFLAGS) -o $@ $^

SOURCES = spawn_inodes.c gen_extents.c resv.h resv_unwritten.c punch_hole.c

DIST_FILES = $(SOURCES)

BIN_EXTRA = fillup_contig_bg.sh

BIN_PROGRAMS = spawn_inodes gen_extents resv_unwritten punch_hole

spawn_inode: spawn_inodes.c
	$(LINK) $(OCFS2_LIBS)

gen_extents: gen_extents.c
	$(LINK) $(OCFS2_LIBS)

resv_unwritten: resv_unwritten.c
	$(LINK) $(OCFS2_LIBS)

punch_hole: punch_hole.c
	$(LINK) $(OCFS2_LIBS)

include $(TOPDIR)/Postamble.make
