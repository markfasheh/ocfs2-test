#
# Fedora 9
#

TESTARCH = $(shell $(TOPDIR)/vendor/fc9/rpmarch.guess tools $(TOPDIR))
VENDOR_EXTENSION = fc9

include $(TOPDIR)/vendor/common/Vendor.make

packages: rpm
