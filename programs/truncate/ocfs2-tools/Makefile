TOPDIR = ../../../

include $(TOPDIR)/Preamble.make

TESTS = test_truncate

CFLAGS = -O2 -Wall -g

SOURCES = test_truncate.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES) ocfs2_truncate.sh

BIN_PROGRAMS = test_truncate

BIN_EXTRA = ocfs2_truncate.sh

test_truncate: $(OBJECTS)
	$(LINK)  -locfs2 -lcom_err

include $(TOPDIR)/Postamble.make
