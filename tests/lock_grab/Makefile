TOPDIR = ../..

include $(TOPDIR)/Preamble.make
include $(TOPDIR)/Test.make

TESTS = lock_grab

CFLAGS = -O2 -Wall -g $(O2DLM_CFLAGS)


lock_grab: lock_grab.o
	$(LINK) $(O2DLM_LIBS)

include $(TOPDIR)/Postamble.make
