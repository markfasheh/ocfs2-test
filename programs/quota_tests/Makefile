TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = quota_multi_tests

CC = $(MPICC)

CFLAGS = -O2 -Wall -g $(O2DLM_CFLAGS) $(OCFS2_CFLAGS)

SOURCES = quota_multi_tests.c quota.h
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES) open_delete.py

BIN_EXTRA = run_quota_multi_tests.py quota_tests.sh

BIN_PROGRAMS = quota_multi_tests

quota_multi_tests: $(OBJECTS)
	$(LINK) $(OCFS2_LIBS)

include $(TOPDIR)/Postamble.make
