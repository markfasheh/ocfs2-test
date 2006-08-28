TOPDIR = ../..

include $(TOPDIR)/Preamble.make

SBIN_PROGRAMS = fsx

CFILES = fsx-linux.c

OBJS = $(subst .c,.o,$(CFILES))

fsx: $(OBJS)
	$(LINK)

include $(TOPDIR)/Postamble.make
