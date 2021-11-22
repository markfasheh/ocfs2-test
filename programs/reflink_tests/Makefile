TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = reflink_tests

CFLAGS = -O2 -Wall -g $(OCFS2_CFLAGS)

CFLAGS += $(EXTRA_CFLAGS)

INCLUDES = -I$(TOPDIR)/programs/libocfs2test

CFLAGS += $(INCLUDES)

LIBO2TEST = $(TOPDIR)/programs/libocfs2test/libocfs2test.a

ifdef NO_REFLINK
CFLAGS += -DNO_REFLINK
endif

MPI_LINK = $(MPICC) $(CFLAGS) $(LDFLAGS) -o $@ $^

SOURCES =			\
	reflink_test.h 		\
	crc32table.h 		\
	xattr_test.h 		\
	reflink_test_utils.c	\
	xattr_test_utils.c 	\
	reflink_test.c 		\
	multi_reflink_test.c 	\
	compat_reflink.c

SINGLE_SOURCES =		\
	reflink_test.c		\
	reflink_test_utils.c	\
	xattr_test_utils.c	\
	xattr_test.h		\
	reflink_test.h

MULTI_SOURCES =			\
	multi_reflink_test.c	\
	reflink_test_utils.c	\
	xattr_test_utils.c	\
	xattr_test.h		\
	reflink_test.h

ifdef NO_REFLINK
SINGLE_SOURCES += compat_reflink.c
MULTI_SOURCES += compat_reflink.c
endif

DIST_FILES = $(SOURCES) reflink_test_run.sh multi_reflink_test_run.sh

BIN_EXTRA = reflink_test_run.sh multi_reflink_test_run.sh reflink_files.sh multi_reflink_snap.sh

BIN_PROGRAMS = reflink_test multi_reflink_test

reflink_test: $(SINGLE_SOURCES)
	$(LINK) $(OCFS2_LIBS) $(LIBO2TEST) -laio

multi_reflink_test: $(MULTI_SOURCES)
	$(MPI_LINK) $(OCFS2_LIBS) $(LIBO2TEST) -laio

include $(TOPDIR)/Postamble.make

