#
# Makefile to configure config.py and create the directories workfiles and log.

CONFIG_SCRIPT := setup.sh

CURDIR = $(shell pwd)

config-script: $(CONFIG_SCRIPT)
ifdef CONFIG_SCRIPT
	$(SHELL) $(CURDIR)/$(CONFIG_SCRIPT) $(DESTDIR)
endif

	mkdir -p  $(DESTDIR)/workfiles $(DESTDIR)/log
	chmod 1777 $(DESTDIR)/log

install: config-script
