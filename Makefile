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

TOOLSARCH = $(shell $(TOPDIR)/rpmarch.guess tools $(TOPDIR))

ifeq ($(TOOLSARCH),error)
$(error could not detect architecture for tools)
endif

SUBDIRS = programs utilities tests suites


DIST_FILES = \
	COPYING					\
	README					\
	Config.make.in				\
	Preamble.make				\
	Postamble.make				\
	aclocal.m4				\
	python.m4				\
	runlog.m4				\
	config.guess				\
	config.sub				\
	configure				\
	configure.in				\
	install-sh				\
	mkinstalldirs				\
	rpmarch.guess

srpm: dist
	$(RPMBUILD) -bs --define "_sourcedir $(RPM_TOPDIR)" --define "_srcrpmdir $(RPM_TOPDIR)" --define "pygtk_name $(PYGTK_NAME)" --define "pyversion $(PYVERSION)" --define "chkconfig_dep $(CHKCONFIG_DEP)" --define "compile_py $(COMPILE_PY)" $(TOPDIR)/vendor/common/ocfs2-tools.spec

rpm: srpm
	$(RPMBUILD) --rebuild --define "pygtk_name $(PYGTK_NAME)" --define "pyversion $(PYVERSION)" --define "chkconfig_dep $(CHKCONFIG_DEP)" --define "compile_py $(COMPILE_PY)" $(TOOLSARCH) "ocfs2-tools-$(DIST_VERSION)-$(RPM_VERSION).src.rpm"

def:
	@echo $(TOOLSARCH)

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
