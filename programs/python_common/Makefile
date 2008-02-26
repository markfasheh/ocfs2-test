TOPDIR = ../..

CURDIR = $(shell pwd)

pre-install:

include $(TOPDIR)/Preamble.make

SUBDIRS = 

DIST_FILES = 	config_py.skel \
		config_shell.skel \
		o2tf.py \
		single_run.sh \
		remote_mount.py \
		remote_umount.py \
		command.py

BIN_EXTRA = 	config_py.skel \
		config_shell.skel \
		o2tf.py \
		single_run.sh \
		remote_mount.py \
		remote_umount.py \
		command.py

post-install:
include $(TOPDIR)/Postamble.make

config: pre-install post-install

include $(CURDIR)/Config.make 

clean:
