TOPDIR = ../..

ARCH = $(shell uname -m)

ifeq ($(ARCH),i686)
TARGET = linux
else
TARGET = linux-$(ARCH)
endif

include $(TOPDIR)/Preamble.make

SUBDIR = iozone3_263/src/current

all:
	$(MAKE) -C $(SUBDIR) $(TARGET)

include $(TOPDIR)/Postamble.make


