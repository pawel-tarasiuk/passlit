include config.mk

LIBRARY = libpasslit.a.$(VERSION)
LIBRARY_LINKS = libpasslit.a libpasslit.a.$(ABI)

.PHONY: all test check sanitize stress sanitize-stress archive clean
.PHONY: distclean memcheck version
all: $(LIBRARY) $(LIBRARY_LINKS) passlit $(call DEBUG_FILE,passlit)

include install.mk

version:
	@printf "%s\n" "$(VERSION)"

passlit.o sampling.o platform.o: PICFLAGS = -fPIC

libpasslit.o: passlit.o sampling.o platform.o
	$(CC) -r -nostdlib -no-pie -o $@ $^

$(LIBRARY): libpasslit.o Makefile
	$(RM) -- $@
	$(AR) $(ARFLAGS) $@ libpasslit.o

libpasslit.a.$(ABI): $(LIBRARY)
	ln -sf -- $(LIBRARY) $@

libpasslit.a: libpasslit.a.$(ABI)
	ln -sf -- $< $@

passlit $(call DEBUG_FILE,passlit) &: tool.o $(LIBRARY) config.mk
	$(CC) $(CFLAGS) $(DEBUG_FLAGS) $(LDFLAGS) \
		-o passlit tool.o $(LIBRARY) $(LDLIBS)
	$(call SPLIT_DEBUG,passlit)

%.o: %.c passlit.h platform.h sampling.h config.mk Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PICFLAGS) $(DEBUG_FLAGS) $(WARNINGS) \
		-c -o $@ $<

test check:
	$(MAKE) -C test check

sanitize:
	$(MAKE) -C test sanitize

memcheck:
	$(MAKE) -C test memcheck

stress sanitize-stress:
	$(MAKE) -C test $@

ARCHIVE_IGNORES = $(filter-out \#%,$(file <.gitignore))
ARCHIVE_EXCLUDES = $(patsubst /%,./%,$(patsubst %/,%,$(ARCHIVE_IGNORES)))

archive:
	@set -eu; ARCHIVE=$$(mktemp /tmp/passlit-archive.XXXXXX); \
		trap '$(RM) -- "$$ARCHIVE"' EXIT HUP INT TERM; \
		$(TAR) --create --file="$$ARCHIVE" \
		--sort=name --owner=0 --group=0 --numeric-owner \
		$(if $(SOURCE_DATE_EPOCH),--mtime="@$(SOURCE_DATE_EPOCH)") \
		--use-compress-program="$(XZ) --threads=1 -9e" \
		--transform="flags=rh;s,^\.,passlit-$(VERSION)," \
		--exclude=".git" \
		$(foreach PAT,$(ARCHIVE_EXCLUDES),--exclude="$(PAT)") -- .; \
		chmod 644 "$$ARCHIVE"; \
		mv -f -- "$$ARCHIVE" "passlit-$(VERSION).tar.xz"

clean:
	$(RM) passlit *.o libpasslit.a* libpasslit.so* libpasslit.dwo* *.dbg passlit.pc
	$(MAKE) -C test clean
	$(MAKE) -C python clean

distclean: clean
	$(MAKE) -C python distclean
	$(RM) -- passlit-*.tar.xz
