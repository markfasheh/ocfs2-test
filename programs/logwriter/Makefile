TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = logwriter enospc_test

CFLAGS = -O2 -Wall -g -D_GNU_SOURCE

LOGWRITER_SOURCES = logwriter.c
LOGWRITE_OBJECTS = $(patsubst %.c,%.o,$(LOGWRITER_SOURCES))
ENOSPC_TEST_SOURCES = enospc_test.c
ENOSPC_TEST_OBJECTS = $(patsubst %.c,%.o,$(ENOSPC_TEST_SOURCES))

SOURCES = $(LOGWRITER_SOURCES) $(ENOSPC_TEST_SOURCES) 

DIST_FILES = $(SOURCES)

BIN_PROGRAMS = logwriter enospc_test

BIN_EXTRA = enospc.sh rename_write_race.sh

logwriter: $(LOGWRITE_OBJECTS)
	$(LINK) 

enospc_test: $(ENOSPC_TEST_OBJECTS)
	$(LINK) 

include $(TOPDIR)/Postamble.make
