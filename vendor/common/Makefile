TOPDIR = ../..

include $(TOPDIR)/Preamble.make

# Depend on the substitution values
$(SCRIPTS): $(TOPDIR)/Config.make

$(patsubst %.sh,%,$(SCRIPT_SH)) : % : %.sh
	rm -f $@ $@+
	sed -e 's/@@VERSION@@/$(VERSION)/g' $@.sh >$@+
	chmod +x $@+
	mv $@+ $@

DIST_FILES =					\
	ocfs2-test.spec-generic.in		\
	Vendor.make

include $(TOPDIR)/Postamble.make
