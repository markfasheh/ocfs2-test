TOPDIR = ../..

include $(TOPDIR)/Preamble.make

LIBRARIES = libocfs2test.a

CFLAGS += -fPIC

CFILES =		\
	dir_ops.c	\
	xattr_ops.c

ifdef OCFS2_TEST_REFLINK
CFILES +=	file_ops.c
endif

HFILES =		\
	dir_ops.h	\
	xattr_ops.h

ifdef OCFS2_TEST_REFLINK
HFILES +=	file_ops.h
endif

mpi_ops.o: mpi_ops.c mpi_ops.h
	$(MPICC) -c -o mpi_ops.o mpi_ops.c $(CFLAGS)

OBJS = $(subst .c,.o,$(CFILES))	\
	mpi_ops.o

$(LIBRARIES): $(OBJS)
	rm -f $@
	$(AR) r $@ $^
	$(RANLIB) $@

DIST_FILES = $(CFILES) $(HFILES)

include $(TOPDIR)/Postamble.make

