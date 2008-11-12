#
# SLES 9
#

TESTARCH = $(shell $(TOPDIR)/vendor/sles9/rpmarch.guess tools $(TOPDIR))
VENDOR_EXTENSION = SLE9

include $(TOPDIR)/vendor/common/Vendor.make

packages: rpm
