TOPDIR = ../..

include $(TOPDIR)/Preamble.make

TESTS = write_torture

CFLAGS = -O2 -Wall -g

SOURCES = write_torture.c  

OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DIST_FILES = $(SOURCES) write_torture.py run_write_torture.py

BIN_PROGRAMS = write_torture

BIN_EXTRA = write_torture.py run_write_torture.py

write_torture: $(OBJECTS)
	$(LINK) 

include $(TOPDIR)/Postamble.make
