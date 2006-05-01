TOPDIR = ../..

include $(TOPDIR)/Preamble.make
include $(TOPDIR)/Test.make

TESTS = extend_and_write verify

CFLAGS = -O2 -Wall -g

EXTEND_AND_WRITE_SOURCES = extend_and_write.c
EXTEND_AND_WRITE_OBJECTS = $(patsubst %.c,%.o,$(EXTEND_AND_WRITE_SOURCES))
VERIFY_SOURCES = verify.c
VERIFY_OBJECTS = $(patsubst %c,%o,$(VERIFY_SOURCES))

SOURCES = $(EXTEND_AND_WRITE_SOURCES) $(VERIFY_SOURCES)

DIST_FILES = $(SOURCES)

extend_and_write: $(EXTEND_AND_WRITE_OBJECTS)
	$(LINK) 
verify: $(VERIFY_OBJECTS)
	$(LINK) 

include $(TOPDIR)/Postamble.make
