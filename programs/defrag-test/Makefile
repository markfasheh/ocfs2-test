TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = defrag-test

CFLAGS = -O2 -Wall -g $(OCFS2_CFLAGS)

CFLAGS += $(EXTRA_CFLAGS)

INCLUDES = -I$(TOPDIR)/programs/libocfs2test

CFLAGS += $(INCLUDES)

LIBO2TEST = $(TOPDIR)/programs/libocfs2test/libocfs2test.a

MPI_LINK = $(MPICC) $(CFLAGS) $(LDFLAGS) -o $@ $^

SOURCES = frager.c verify_file.c defrager.c

MULTI_SOURCES = multi_defrager.c

DIST_FILES = $(SOURCES) $(MULTI_SOURCES)

BIN_PROGRAMS = frager verify_file defrager multi_defrager

frager: frager.o
	$(LINK) $(OCFS2_LIBS) $(LIBO2TEST)

verify_file: verify_file.o
	$(LINK) $(OCFS2_LIBS) $(LIBO2TEST)

defrager: defrager.o
	$(LINK) $(OCFS2_LIBS)

multi_defrager: $(MULTI_SOURCES)
	$(MPI_LINK) $(OCFS2_LIBS) $(LIBO2TEST)

include $(TOPDIR)/Postamble.make
