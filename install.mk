STAGE = $(if $(DESTDIR),$(abspath $(DESTDIR)))

.PHONY: install FORCE
install: all passlit.pc
	$(INSTALL) -d -- "$(STAGE)$(abspath $(BINDIR))" \
		"$(STAGE)$(abspath $(LIBDIR))" \
		"$(STAGE)$(abspath $(INCLUDEDIR))" \
		"$(STAGE)$(abspath $(MAN1DIR))" \
		"$(STAGE)$(abspath $(MAN3DIR))" \
		"$(STAGE)$(abspath $(PKGCONFIGDIR))" \
		"$(STAGE)$(abspath $(LICENSEDIR))"
	$(INSTALL_PROGRAM) -- passlit "$(STAGE)$(abspath $(BINDIR))/passlit"
	$(INSTALL_DATA) -- $(LIBRARY) "$(STAGE)$(abspath $(LIBDIR))/$(LIBRARY)"
	ln -sf -- "$(LIBRARY)" \
		"$(STAGE)$(abspath $(LIBDIR))/libpasslit.a.$(ABI)"
	ln -sf -- "libpasslit.a.$(ABI)" \
		"$(STAGE)$(abspath $(LIBDIR))/libpasslit.a"
	$(INSTALL_DATA) -- passlit.h \
		"$(STAGE)$(abspath $(INCLUDEDIR))/passlit.h"
	$(INSTALL_DATA) -- man/passlit.1 "$(STAGE)$(abspath $(MAN1DIR))/passlit.1"
	$(INSTALL_DATA) -- man/passlit.3 "$(STAGE)$(abspath $(MAN3DIR))/passlit.3"
	$(INSTALL_DATA) -- passlit.pc \
		"$(STAGE)$(abspath $(PKGCONFIGDIR))/passlit.pc"
	$(INSTALL_DATA) -- LICENSE.md COPYING.md COPYING.LESSER.md \
		"$(STAGE)$(abspath $(LICENSEDIR))/"
ifeq ($(DEBUG),2)
	$(INSTALL_DATA) -- passlit.dbg \
		"$(STAGE)$(abspath $(BINDIR))/passlit.dbg"
endif

passlit.pc: FORCE
	printf "%s\n" \
		"prefix=$(abspath $(PREFIX))" \
		"exec_prefix=$(abspath $(EXEC_PREFIX))" \
		"libdir=$(abspath $(LIBDIR))" \
		"includedir=$(abspath $(INCLUDEDIR))" \
		"" \
		"Name: passlit" \
		"Description: Readable passwords and codes" \
		"Version: $(VERSION)" \
		"Requires: gmp libcrypto" \
		'Libs: -L"$${libdir}" -lpasslit' \
		'Cflags: -I"$${includedir}"' > "$@"
