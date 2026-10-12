CC ?= cc
AR ?= ar
ifeq ($(origin ARFLAGS),default)
	ARFLAGS = rcs
endif
ARFLAGS ?= rcs
CFLAGS ?= -Os
CFLAGS += -fhardened -fPIE -fno-lto
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -fvect-cost-model=cheap
ifneq ($(filter x86_64-%,$(shell $(CC) -dumpmachine)),)
	ARCH_FLAGS ?= -march=x86-64-v3 -mshstk
	CFLAGS += $(ARCH_FLAGS)
endif
CPPFLAGS ?=
LDFLAGS ?=
LDFLAGS += -Wl,-z,relro,-z,now,--hash-style=both -fno-lto
LDFLAGS += -Wl,--gc-sections
WARNINGS ?= -std=c99 -pedantic-errors -Wall -Wextra -Wconversion -Wshadow
RM ?= rm -f
PKG_CONFIG ?= pkg-config
STATIC ?= 0
THREADS ?= 1

PREFIX ?= /usr/local
EXEC_PREFIX ?= $(PREFIX)
BINDIR ?= $(EXEC_PREFIX)/bin
LIBDIR ?= $(EXEC_PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
DATAROOTDIR ?= $(PREFIX)/share
MANDIR ?= $(DATAROOTDIR)/man
MAN1DIR ?= $(MANDIR)/man1
MAN3DIR ?= $(MANDIR)/man3
PKGCONFIGDIR ?= $(LIBDIR)/pkgconfig
LICENSEDIR ?= $(DATAROOTDIR)/licenses/passlit
DESTDIR ?=
INSTALL ?= install
INSTALL_PROGRAM ?= $(INSTALL) -m 755
INSTALL_DATA ?= $(INSTALL) -m 644
TAR ?= tar
XZ ?= xz

ifeq ($(STATIC),1)
	LDFLAGS += -static-pie
	LDLIBS ?= $(shell $(PKG_CONFIG) --libs --static gmp libcrypto)
else
	LDFLAGS += -pie
	LDLIBS ?= -lgmp -lcrypto
endif

DEBUG ?= 0
ifeq ($(THREADS),1)
	CPPFLAGS += -DPASSLIT_THREADS
	CFLAGS += -pthread
	LDLIBS += -pthread
endif

OBJCOPY ?= objcopy
ifneq ($(filter 1 2,$(DEBUG)),)
	DEBUG_FLAGS = -g2
else
	DEBUG_FLAGS = -g0
endif

DEBUG_FILE = $(if $(filter 2,$(DEBUG)),$(1).dbg)
ifeq ($(DEBUG),2)
define SPLIT_DEBUG
	$(OBJCOPY) --only-keep-debug $(1) $(1).dbg
	$(OBJCOPY) --strip-all $(1)
	$(OBJCOPY) --add-gnu-debuglink=$(1).dbg $(1)
endef
endif

VERSION = $(shell V=$$(sed -n \
	's/^\#define PASSLIT_VERSION \(0x[0-9a-fA-F]*\)U.*/\1/p' passlit.h); \
	printf '%d.%d.%d' $$((V >> 24)) $$(((V >> 16) & 255)) \
	$$(((V >> 8) & 255)))
ABI = $(firstword $(subst ., ,$(VERSION)))
