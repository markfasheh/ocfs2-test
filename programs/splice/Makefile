TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = splice_read splice_write

CFLAGS = -O2 -Wall -g

SPLICE_READ_SOURCES = splice_read.c
SPLICE_READ_OBJECTS = $(patsubst %.c,%.o,$(SPLICE_READ_SOURCES))
SPLICE_WRITE_SOURCES = splice_write.c
SPLICE_WRITE_OBJECTS = $(patsubst %.c,%.o,$(SPLICE_WRITE_SOURCES))

SOURCES = $(SPLICE_WRITE_SOURCES) $(SPLICE_READ_SOURCES)

DIST_FILES = $(SOURCES) splice_test.py

BIN_PROGRAMS = splice_read splice_write

BIN_EXTRA = splice_test.py

splice_read: $(SPLICE_READ_OBJECTS)
	$(LINK) 
splice_write: $(SPLICE_WRITE_OBJECTS)
	$(LINK) 

include $(TOPDIR)/Postamble.make
