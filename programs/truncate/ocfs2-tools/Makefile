TOPDIR = ../../../

include $(TOPDIR)/Preamble.make

TESTS = test_truncate

CFLAGS = -O2 -Wall -g
OCFS2_LIBS=`pkg-config --cflags --libs ocfs2`

SOURCES = test_truncate.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES) ocfs2_truncate.sh

BIN_PROGRAMS = test_truncate

BIN_EXTRA = ocfs2_truncate.sh

test_truncate: $(OBJECTS)
	$(LINK) $(OCFS2_LIBS)

include $(TOPDIR)/Postamble.make
