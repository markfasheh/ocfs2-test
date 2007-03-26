TOPDIR = ../..

ARCH = $(shell uname -m)

include $(TOPDIR)/Preamble.make

SUBDIR = bonnie++-1.03a

	
all: bonnie

stamp-configure-$(ARCH):
	rm -f stamp-configure-* 2>/dev/null
	cd $(SUBDIR) && ./configure
	touch stamp-configure-$(ARCH)
	
configure: stamp-configure-$(ARCH)

bonnie: configure
	$(MAKE) -C $(SUBDIR)

clean-bonnie:
	$(MAKE) -C $(SUBDIR) clean
	rm -f stamp-configure-* 2>/dev/null

CLEAN_RULES = clean-bonnie

include $(TOPDIR)/Postamble.make

