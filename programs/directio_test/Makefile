TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = directio_test

CFLAGS = -O2 -Wall -g $(OCFS2_CFLAGS)

CFLAGS += $(EXTRA_CFLAGS)

MPI_LINK = $(MPICC) $(CFLAGS) $(LDFLAGS) -o $@ $^

SOURCES =			\
	directio.h 		\
	crc32table.h		\
	directio_utils.c	\
	directio_test.c

MULTI_SOURCES =			\
	directio.h		\
	crc32table.h		\
	directio_utils.c	\
	multi_directio_test.c

DIST_FILES = $(SOURCES) multi_diretio_test.c

BIN_PROGRAMS = directio_test multi_directio_test

directio_test: $(SOURCES)
	$(LINK) $(OCFS2_LIBS)

multi_directio_test: $(MULTI_SOURCES)
	$(MPI_LINK) $(OCFS2_LIBS)

include $(TOPDIR)/Postamble.make
