SOURCE_DIR ?= $(if $(wildcard src/passlit.h),src,..)
include $(SOURCE_DIR)/config.mk

PYTHON ?= python3
OUT ?= passlit.abi3.so
PY_INCLUDES = $(shell $(PYTHON) -c \
	'import sysconfig; print("-I" + sysconfig.get_path("include"))')
PY_API = -DPy_LIMITED_API=0x030D0000
ifneq ($(PASSLIT_DEPS),)
	CPPFLAGS += -isystem $(PASSLIT_DEPS)/include
	PY_LIBS = $(PASSLIT_DEPS)/lib/libgmp.a \
		$(PASSLIT_DEPS)/lib/libcrypto.a -ldl -pthread
else
	PY_LIBS = -lgmp -lcrypto
endif

.PHONY: all test clean distclean
all: $(OUT)

module.c: passlit.pyx
	$(PYTHON) -m cython -3 --module-name passlit -o $@ $<

module.o: module.c $(SOURCE_DIR)/passlit.h $(SOURCE_DIR)/config.mk Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -fvisibility=hidden $(PY_API) \
		$(PY_INCLUDES) -I"$(SOURCE_DIR)" $(DEBUG_FLAGS) -std=c99 -Wall -Wextra \
		-c -o $@ $<

%.o: $(SOURCE_DIR)/%.c $(SOURCE_DIR)/passlit.h $(SOURCE_DIR)/sampling.h \
	$(SOURCE_DIR)/platform.h $(SOURCE_DIR)/config.mk Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -fvisibility=hidden \
		$(DEBUG_FLAGS) $(WARNINGS) -c -o $@ $<

$(OUT): module.o passlit.o sampling.o platform.o exports.map
	mkdir -p -- "$(dir $(OUT))"
	$(CC) $(CFLAGS) -fPIC -shared \
		-Wl,-z,relro,-z,now,-z,text,--hash-style=both \
		-Wl,--gc-sections,--exclude-libs,ALL \
		-Wl,--version-script=exports.map \
		-o "$(OUT)" module.o passlit.o sampling.o platform.o \
		$(PY_LIBS)

test: all
	PYTHONPATH="$(abspath $(dir $(OUT)))" $(PYTHON) test_passlit.py

clean:
	$(RM) -- module.c *.o *.so *.su

distclean: clean
	$(RM) -r -- build dist deps *.egg-info
