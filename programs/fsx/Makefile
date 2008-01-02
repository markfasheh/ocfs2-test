TOPDIR = ../..

include $(TOPDIR)/Preamble.make

BIN_PROGRAMS = fsx

CFILES = fsx-linux.c

OBJS = $(subst .c,.o,$(CFILES))

fsx: $(OBJS)
	$(LINK)

include $(TOPDIR)/Postamble.make
