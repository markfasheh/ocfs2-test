TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = check_file_size_limits

CFLAGS = -O2 -Wall -g $(OCFS2_CFLAGS)

SOURCES = check_file_size_limits.c

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = check_file_size_limits

check_file_size_limits: check_file_size_limits.o
	$(LINK) $(OCFS2_LIBS)

include $(TOPDIR)/Postamble.make
