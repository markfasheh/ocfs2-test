#
# SLES 10
#

TESTARCH = $(shell $(TOPDIR)/vendor/sles10/rpmarch.guess tools $(TOPDIR))
VENDOR_EXTENSION = SLE10

include $(TOPDIR)/vendor/common/Vendor.make

packages: rpm
