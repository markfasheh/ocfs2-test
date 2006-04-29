
#
# Makefile snipped for ocfs2 tests
# 
# This snippet makes any binary that is part of the TESTS
# variable get built and installed in $(TESTDIR)
#

# Put the tests in UNINST_PROGRAMS, because they don't go in /usr/bin
UNINST_PROGRAMS = $(TESTS)

INSTALL_RULES = install-tests

install-tests: $(TESTS)
ifdef TESTS
	$(SHELL) $(TOPDIR)/mkinstalldirs $(DESTDIR)$(libdir)/ocfs2-tests
	for prog in $(TESTS); do \
	  $(INSTALL_PROGRAM) $$prog $(DESTDIR)$(libdir)/ocfs2-tests/$$prog; \
	done
endif


