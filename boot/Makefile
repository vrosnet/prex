TARGET=		bootldr
TYPE=		BINARY
SUBDIR=		lib $(ARCH) common
OBJS=		./$(ARCH)/$(PLATFORM)/boot.o ./common/common.o
LIBS=		./lib/libboot.a
LD_SCRIPT=	./$(ARCH)/$(ARCH)/boot.ld

include $(SRCDIR)/mk/boot.mk
