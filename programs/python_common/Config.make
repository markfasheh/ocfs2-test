#
# Makefile to configure config.py and create the directories workfiles and log.

CONFIG_SCRIPT := setup.sh

CURDIR = $(shell pwd)

ifdef RPM_BUILD_ROOT
INSTALLDIR = /usr/local/ocfs2-test
endif

config-script: $(CONFIG_SCRIPT) 

ifdef CONFIG_SCRIPT
	$(SHELL) $(CURDIR)/$(CONFIG_SCRIPT) $(DESTDIR) $(INSTALLDIR)
endif

	mkdir -p  $(DESTDIR)/workfiles $(DESTDIR)/log $(DESTDIR)/tmp
	chmod 1777 $(DESTDIR)/log $(DESTDIR)/tmp

install: config-script
