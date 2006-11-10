#
# Makefile to configure config.py

CONFIG_SCRIPT := config.sh

CURDIR = $(shell pwd)

config-script: $(CONFIG_SCRIPT)
ifdef CONFIG_SCRIPT
	$(SHELL) $(CURDIR)/$(CONFIG_SCRIPT) $(DESTDIR)
endif

install: config-script
