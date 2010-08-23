TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = directio_test

CFLAGS = -O2 -Wall -g $(OCFS2_CFLAGS)

CFLAGS += $(EXTRA_CFLAGS)

ifdef NO_REFLINK
CFLAGS += -DNO_REFLINK
endif

SOURCES =			\
	directio.h 		\
	crc32table.h		\
	directio_utils.c	\
	directio_test.c

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = directio_test

directio_test: $(SOURCES)
	$(LINK) $(OCFS2_LIBS)

include $(TOPDIR)/Postamble.make
