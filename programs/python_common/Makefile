TOPDIR = ../..

CURDIR = $(shell pwd)

pre-install:

include $(TOPDIR)/Preamble.make

SUBDIRS = workfiles

DIST_FILES = config.py o2tf.py

BIN_EXTRA = config.py o2tf.py

post-install:
include $(TOPDIR)/Postamble.make

config: pre-install post-install

include $(CURDIR)/Config.make 

clean:
