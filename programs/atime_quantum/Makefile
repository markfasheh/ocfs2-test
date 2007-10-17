TOPDIR = ../..

CURDIR = $(shell pwd)

pre-install:

include $(TOPDIR)/Preamble.make

SUBDIRS = 

DIST_FILES = test_atime_quantum.py

BIN_EXTRA = test_atime_quantum.py

post-install:
include $(TOPDIR)/Postamble.make

config: pre-install post-install

include $(CURDIR)/Config.make 

clean:
