TOPDIR = ../..

ARCH = $(shell uname -m)

ifeq ($(ARCH),i686)
TARGET = linux
else 
   ifeq ($(ARCH),ppc64)
      TARGET = linux-powerpc64
   else 
      ifeq ($(ARCH),x86_64)
         TARGET = linux-AMD64
      else
         ifeq ($(ARCH),s390x)
            TARGET = linux-S390X
         else
            TARGET = linux-$(ARCH)
         endif
      endif
  endif
endif

include $(TOPDIR)/Preamble.make

SUBDIR = iozone3_263/src/current/

all:
	$(MAKE) -C $(SUBDIR) $(TARGET)

include $(TOPDIR)/Postamble.make

clean:
	$(MAKE) -C $(SUBDIR) clean
