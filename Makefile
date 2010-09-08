TOPDIR = .
 
include $(TOPDIR)/Preamble.make

RPM_TOPDIR = $(CURDIR)

RPMBUILD = $(shell /usr/bin/which rpmbuild 2>/dev/null || /usr/bin/which rpm 2>/dev/null || echo /bin/false)

SUSEBUILD = $(shell if test -r /etc/SuSE-release; then echo yes; else echo no; fi)

PYVERSION = $(shell echo $(pyexecdir) | sed -e 's/.*python\([0-9]\.[0-9]\).*/\1/')

ifeq ($(SUSEBUILD),yes)
COMPILE_PY = 0
else
COMPILE_PY = 1
endif

TESTARCH = $(shell $(TOPDIR)/rpmarch.guess tools $(TOPDIR))

ifeq ($(TESTARCH),error)
$(error could not detect architecture for ocfs2-test)
endif

SUBDIRS = api-compat programs utilities tests suites

SUBDIRS += vendor

API_COMPAT_FILES = \
	api-compat/include/splice.h	\
	api-compat/include/reflink.h

DIST_FILES = \
	COPYING					\
	CREDITS					\
	MAINTAINERS				\
	README					\
	Config.make.in				\
	Preamble.make				\
	Postamble.make				\
	aclocal.m4				\
	mbvendor.m4				\
	python.m4				\
	pythondev.m4				\
	runlog.m4				\
	config.guess				\
	config.sub				\
	configure				\
	configure.in				\
	install-sh				\
	mkinstalldirs				\
	Vendor.make				\
	vendor.guess				\
	svnrev.guess				\
	rpmarch.guess

DIST_RULES = dist-subdircreate

.PHONY: dist dist-subdircreate dist-bye dist-fresh distclean

dist-subdircreate:
	$(TOPDIR)/mkinstalldirs $(DIST_DIR)/documentation/samples
	$(TOPDIR)/mkinstalldirs $(DIST_DIR)/debian

dist-bye:
	-rm -rf $(DIST_TOPDIR)

dist-fresh: dist-bye
	$(TOPDIR)/mkinstalldirs $(DIST_TOPDIR)

dist: dist-fresh dist-all
	GZIP=$(GZIP_OPTS) tar chozf $(DIST_TOPDIR).tar.gz $(DIST_TOPDIR)
	$(MAKE) dist-bye

distclean: clean
	rm -f Config.make config.status config.cache config.log $(PKGCONFIG_FILES)

INSTALL_RULES = install-pkgconfig

install-pkgconfig: $(PKGCONFIG_FILES)
	$(SHELL) $(TOPDIR)/mkinstalldirs $(DESTDIR)$(libdir)/pkgconfig
	for p in $(PKGCONFIG_FILES); do \
	  $(INSTALL_DATA) $$p $(DESTDIR)$(libdir)/pkgconfig/$$p; \
	done


include Vendor.make

def:
	@echo $(TESTARCH)

include $(TOPDIR)/Postamble.make

install: install-done


install-done:
	@echo
	@echo "==========================================================================="
	@echo
	@echo " ocfs2-test now requires sudo setup."
	@echo
	@echo " Please make sure the user that will run ocfs2-test has the following sudo"
	@echo " privileges granted without prompting for password on all nodes:"
	@echo
	@echo "		/sbin/fuser"
	@echo "		/sbin/mkfs.ocfs2"
	@echo "		/sbin/fsck.ocfs2"
	@echo "		/sbin/tunefs.ocfs2"
	@echo "		/sbin/debugfs.ocfs2"
	@echo "		/sbin/mounted.ocfs2"
	@echo "		/bin/mount"
	@echo "		/bin/umount"
	@echo "		/bin/chmod"
	@echo "		/bin/chown"
	@echo "		/bin/mkdir"
	@echo
	@echo " For security, enable the privileges only when tests are executed."
	@echo "==========================================================================="
