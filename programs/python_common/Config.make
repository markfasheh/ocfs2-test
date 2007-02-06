#
# Makefile to configure config.py and create the directories workfiles and log.

CONFIG_SCRIPT := config.sh

CURDIR = $(shell pwd)

config-script: $(CONFIG_SCRIPT)
ifdef CONFIG_SCRIPT
	$(SHELL) $(CURDIR)/$(CONFIG_SCRIPT) $(DESTDIR)
endif

	mkdir -p  $(DESTDIR)/workfiles $(DESTDIR)/log
install: config-script
