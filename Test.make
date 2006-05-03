
#
# Makefile snipped for ocfs2 tests
# 
# This snippet makes any binary that is part of the TESTS
# variable get built and installed in $(TESTDIR)
#

# Put the tests in UNINST_PROGRAMS, because they don't go in /usr/bin
UNINST_PROGRAMS = $(TESTS)

INSTALL_RULES = install-tests

TEST_PWD = `pwd`
TOPDIR_PWD = `cd $(TOPDIR) && pwd`

install-tests: $(TESTS)
ifdef TESTS
	TEST_LOC="$(TEST_PWD)" ; \
	TEST_LOC="$${TEST_LOC#$(TOPDIR_PWD)/}" ; \
	$(SHELL) $(TOPDIR)/mkinstalldirs $(DESTDIR)$(TESTDIR)/$$TEST_LOC; \
	for prog in $(TESTS); do \
	  $(INSTALL_PROGRAM) $$prog $(DESTDIR)$(TESTDIR)/$$TEST_LOC/$$prog ; \
	done
endif


