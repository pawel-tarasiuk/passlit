include ../config.mk

PROGRAMS = test_passlit test_random test_seed test_limits test_patterns test_rank \
	test_platform test_platform_fallback
DEPS = ../passlit.c ../sampling.c ../passlit.h ../platform.c \
	../platform.h ../sampling.h
SAN_CFLAGS ?= -O1 -g3 -fPIE -fno-omit-frame-pointer -fstack-usage \
	$(ARCH_FLAGS) \
	-fsanitize=address,undefined,bounds-strict -fno-sanitize-recover=all \
	-ffunction-sections -fdata-sections -Wl,--gc-sections
ifeq ($(THREADS),1)
	SAN_CFLAGS += -pthread
endif
MEM_CFLAGS ?= -O1 -g2 -fPIE -ffunction-sections -fdata-sections \
	-Wl,--gc-sections
SAN_LDLIBS ?= -lgmp -lcrypto
VALGRIND ?= valgrind
DEBUGINFOD ?= 0

.PHONY: all test check sanitize memcheck stress sanitize-stress clean library
all: test_consumer $(call DEBUG_FILE,test_consumer) $(PROGRAMS) \
	$(foreach program,$(PROGRAMS),$(call DEBUG_FILE,$(program)))

test_% $(call DEBUG_FILE,test_%) &: test_%.c $(DEPS) ../config.mk Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEBUG_FLAGS) $(WARNINGS) \
		$(LDFLAGS) -o test_$* $< \
		$(if $(filter limits platform,$*),,../platform.c) $(LDLIBS)
	$(call SPLIT_DEBUG,test_$*)

test_platform_fallback $(call DEBUG_FILE,test_platform_fallback) &: \
	test_platform.c ../platform.c ../platform.h ../config.mk Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEBUG_FLAGS) $(WARNINGS) \
		-DPASSLIT_NO_PAGE_QUERY $(LDFLAGS) -o test_platform_fallback $<
	$(call SPLIT_DEBUG,test_platform_fallback)

library:
	$(MAKE) -C .. all

test_consumer $(call DEBUG_FILE,test_consumer) &: test_consumer.c \
	../passlit.h ../config.mk Makefile library
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEBUG_FLAGS) $(WARNINGS) -I.. \
		$(LDFLAGS) -o test_consumer test_consumer.c ../libpasslit.a $(LDLIBS)
	$(call SPLIT_DEBUG,test_consumer)

test check: all
	./test_consumer
	$(SHELL) run.sh

san:
	mkdir -p $@

san/test_%: test_%.c $(DEPS) Makefile | san
	$(CC) $(CPPFLAGS) $(SAN_CFLAGS) $(WARNINGS) -pie -o $@ $< \
		$(if $(filter limits platform,$*),,../platform.c) \
		$(SAN_LDLIBS)

san/test_platform_fallback: test_platform.c ../platform.c ../platform.h \
	Makefile | san
	$(CC) $(CPPFLAGS) $(SAN_CFLAGS) $(WARNINGS) -DPASSLIT_NO_PAGE_QUERY \
		-pie -o $@ $<

san/tool: ../tool.c $(DEPS) Makefile | san
	$(CC) $(CPPFLAGS) $(SAN_CFLAGS) $(WARNINGS) -pie -o $@ \
		../tool.c ../passlit.c ../sampling.c ../platform.c \
		$(SAN_LDLIBS)

sanitize: $(addprefix san/,$(PROGRAMS)) san/tool
	PASSLIT=./san/tool $(SHELL) run.sh ./san

mem:
	mkdir -p $@

mem/tool: ../tool.c $(DEPS) ../config.mk Makefile | mem
	$(CC) $(CPPFLAGS) $(MEM_CFLAGS) $(WARNINGS) -pie \
		$(if $(filter 1,$(THREADS)),-pthread) -o $@ \
		../tool.c ../passlit.c ../sampling.c ../platform.c \
		-lgmp -lcrypto

memcheck: mem/tool
	VALGRIND="$(VALGRIND)" DEBUGINFOD="$(DEBUGINFOD)" \
		$(SHELL) memcheck.sh

stress: test_limits
	./test_limits --full

sanitize-stress: san/test_limits
	./san/test_limits --full

clean:
	$(RM) test_consumer $(PROGRAMS) *.dbg san/* mem/*
	@if test -d san; then rmdir san; fi
	@if test -d mem; then rmdir mem; fi
