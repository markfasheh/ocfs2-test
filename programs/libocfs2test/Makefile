TOPDIR = ../..

include $(TOPDIR)/Preamble.make

LIBRARIES = libocfs2test.a

CFLAGS += -fPIC

CFILES =		\
	file_ops.c	\
	dir_ops.c	\
	xattr_ops.c

HFILES =		\
	file_ops.h	\
	dir_ops.h	\
	xattr_ops.h

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

