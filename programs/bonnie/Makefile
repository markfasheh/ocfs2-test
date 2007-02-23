TOPDIR = ../..

ARCH = $(shell uname -m)

include $(TOPDIR)/Preamble.make

SUBDIR = bonnie++-1.03a

bonnie:
	cd $(SUBDIR) && ./configure
	
all: bonnie
	$(MAKE) -C $(SUBDIR)

include $(TOPDIR)/Postamble.make

clean:
	$(MAKE) -C $(SUBDIR) clean
