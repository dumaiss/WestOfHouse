# Build against the exported KonaOS SDK; no OS checkout is modified.
.DEFAULT_GOAL := all
KOS_SDK_ROOT ?= ../../ZephyrOS/build/kos-sdk-0.2.0
# Accepted M3.5 configuration. ZVM_ASM is the experimental text helper;
# ZVM_CACHE_ASM is the accepted cache fast path and stays enabled by default.
ZVM_PROFILE ?= 0
ZVM_ASM ?= 0
ZVM_CACHE_ASM ?= 1
ZVM_STACK_PROBE ?= 0
ZVM_STACK ?= 512
ifeq ($(ZVM_STACK_PROBE),1)
ifneq ($(ZVM_STACK),512)
$(error ZVM_STACK_PROBE requires ZVM_STACK=512)
endif
endif
DYN_CACHE_PAGES ?= 4
STATIC_CACHE_PAGES ?= 4
ZVM_CONFIG := $(ZVM_PROFILE):$(ZVM_ASM):$(ZVM_CACHE_ASM):$(ZVM_STACK_PROBE):$(ZVM_STACK):$(DYN_CACHE_PAGES):$(STATIC_CACHE_PAGES)
ZVM_RELEASE_CONFIG := 0:0:1:0:512:4:4
ZVM_VARIANT := $(if $(filter 1,$(ZVM_ASM)),$(if $(filter 1,$(ZVM_PROFILE)),profile-asm,asm),$(if $(filter 1,$(ZVM_PROFILE)),profile,c))
ifeq ($(ZVM_CONFIG),$(ZVM_RELEASE_CONFIG))
ZVM_BUILD := build/optimized
else
ZVM_BUILD := build/variants/cache-$(DYN_CACHE_PAGES)-$(STATIC_CACHE_PAGES)/$(ZVM_VARIANT)-s$(ZVM_STACK)$(if $(filter 1,$(ZVM_CACHE_ASM)),-fast,)$(if $(filter 1,$(ZVM_STACK_PROBE)),-stack,)
endif
KOS_BUILD := $(ZVM_BUILD)/target
KOS_LIB := $(KOS_SDK_ROOT)/lib
EXE_LIB_DIR := $(ZVM_BUILD)/lib
# These members invoke callbacks held in zvm_t; the rest of libzvm remains
# eligible for automatic placement into home/far zones.
KOS_POINTER_MEMBERS_zvm := memory.rel text.rel services.rel
include $(KOS_SDK_ROOT)/mk/kos.mk
ZVM_FLAGS := $(if $(filter 1,$(ZVM_PROFILE)),-DZVM_PROFILE=1,) $(if $(filter 1,$(ZVM_ASM)),-DZVM_ASM=1,)
ZVM_FLAGS += -DDYN_CACHE_PAGES=$(DYN_CACHE_PAGES) -DSTATIC_CACHE_PAGES=$(STATIC_CACHE_PAGES)
ZVM_FLAGS += $(if $(filter 1,$(ZVM_STACK_PROBE)),-DZVM_STACK_PROBE=1,)
ZVM_FLAGS += $(if $(filter 1,$(ZVM_CACHE_ASM)),-DZVM_CACHE_ASM=1,)
EXE_CFLAGS += $(ZVM_FLAGS)

SDCC ?= sdcc
SDAR ?= sdar
SDASZ80 ?= sdasz80
CC ?= cc
# Host cache tests reuse the platform implementation; the exported SDK ships
# its target object only. Override this path with a KonaOS source checkout.
KOS_READER_FULL_SOURCE ?= ../../ZephyrOS/user/sdk/src/reader_full.c
# The original fixture stays the default. Reporting runs stage a matching
# generated story/sidecar pair explicitly; ZVMTest reads no YAML or room names.
ZORK_STORY ?= ../vendor/infocom-zork1/COMPILED/zork1.z3
ZORK_BINDINGS ?=
ZORK_ASSETS = --asset $(ZORK_STORY)=ZORK1.Z3 $(if $(strip $(ZORK_BINDINGS)),--asset $(ZORK_BINDINGS)=ZORK1.WOH,)
# SDK card directories persist: keep optional reporting metadata out of the
# original fixture's card when switching between the two launch targets.
ZORK_CARD = $(KOS_BUILD)/card$(if $(strip $(ZORK_BINDINGS)),-state,)
ZVM_SOURCES := zvm memory variables frames objects decode execute opcodes text dictionary input services
ZVM_OBJS := $(addprefix $(KOS_BUILD)/libzvm/,$(addsuffix .rel,$(ZVM_SOURCES)))
ifeq ($(ZVM_ASM),1)
ZVM_OBJS += $(KOS_BUILD)/libzvm/zchar.rel
endif
ZVM_LIB := $(ZVM_BUILD)/lib/libzvm.lib

$(KOS_BUILD)/libzvm/%.rel: src/%.c src/zvm_internal.h include/zvm.h include/zvm_profile.h
	mkdir -p $(dir $@)
	$(SDCC) $(ZVM_FLAGS) -mz80 -c --nostdinc --Werror -I$(KOS_SDK_ROOT)/include/abi -I$(KOS_SDK_ROOT)/include -Iinclude -c $< -o $@
$(KOS_BUILD)/libzvm/%.rel: src/%.asm
	mkdir -p $(dir $@)
	$(SDASZ80) -o $@ $<
$(ZVM_LIB): $(ZVM_OBJS)
	mkdir -p $(dir $@)
	rm -f $@
	$(SDAR) -rcD $@ $(ZVM_OBJS)

$(eval $(call exe_program,zvmtest,tests,zvmtest.c story_cache.c story_cache_z80.c stack_probe.c state_report.c,STACK=$(ZVM_STACK),LIBS=zvm,INCLUDES=include:$(KOS_SDK_ROOT)/include/kos))
# Makefile/SDK edits must not reuse objects compiled with previous settings.
# Include public VM headers for the application too (the SDK tracks tests/*.h).
$(ZVM_OBJS) $(zvmtest_COBJS): Makefile $(KOS_RULES) $(EXE_HEADERS)
$(zvmtest_COBJS): include/zvm.h include/zvm_profile.h
$(KOS_BUILD)/user/zvmtest/zvmtest.rel $(KOS_BUILD)/user/zvmtest/story_cache.rel $(KOS_BUILD)/user/zvmtest/story_cache_z80.rel: tests/story_cache.h
$(KOS_BUILD)/user/zvmtest/zvmtest.rel $(KOS_BUILD)/user/zvmtest/stack_probe.rel: tests/stack_probe.h
.PHONY: all release help publish test test-cache clean run-zork test-zork
all: zvmtest

# Only the accepted configuration may publish the two delivery artifacts.
# Explicit profiling/reference/experimental builds never overwrite them.
ifeq ($(ZVM_CONFIG),$(ZVM_RELEASE_CONFIG))
zvmtest: publish
publish: $(zvmtest_EXE) $(ZVM_LIB)
	@cp $(zvmtest_EXE) build/ZVMTEST.EXE.tmp
	@mv build/ZVMTEST.EXE.tmp build/ZVMTEST.EXE
	@cp $(ZVM_LIB) build/libzvm.lib.tmp
	@mv build/libzvm.lib.tmp build/libzvm.lib
	@cd build && sha256sum ZVMTEST.EXE libzvm.lib > SHA256SUMS
	@printf '%s\n' 'Optimized build ready:' '  $(abspath build/ZVMTEST.EXE)' '  $(abspath build/libzvm.lib)' 'Checksums: $(abspath build/SHA256SUMS)'
else
zvmtest:
	@printf '%s\n' 'Development variant: $(abspath $(zvmtest_EXE))' 'Delivery artifacts unchanged. Use make release for the optimized build.'
publish:
	@printf '%s\n' 'Only the accepted optimized configuration may publish. Use make release.' >&2
	@exit 1
endif

# Force the accepted settings even when invoked from a diagnostic configuration.
release:
	$(MAKE) zvmtest ZVM_PROFILE=0 ZVM_ASM=0 ZVM_CACHE_ASM=1 ZVM_STACK_PROBE=0 ZVM_STACK=512 DYN_CACHE_PAGES=4 STATIC_CACHE_PAGES=4

help:
	@printf '%s\n' \
	  'make / make zvmtest  Build optimized libzvm + ZVMTEST (default settings)' \
	  'make release        Force accepted optimized settings and publish' \
	  'Get the executable: build/ZVMTEST.EXE' \
	  'Get the library:    build/libzvm.lib' \
	  'make test           Host fixtures, target build and package validation' \
	  'make run-zork       Interactive original story through the SDK MAME runner' \
	  'make story          Build generated state-reporting story and ZVMTest bundle' \
	  'make run-state-zork Play the instrumented story with numeric reporting' \
	  'make zvmtest ZVM_PROFILE=1  Separate diagnostic variant, no publishing' \
	  'make clean          Remove all build output (including old runs)'

test: $(ZVM_LIB) test-cache
	mkdir -p $(ZVM_BUILD)/host
	$(CC) $(ZVM_FLAGS) -std=c99 -Wall -Wextra -Werror -Itests/host/include -I$(KOS_SDK_ROOT)/include/abi -I$(KOS_SDK_ROOT)/include -Iinclude -Isrc tests/test_zvm.c $(addprefix src/,$(addsuffix .c,$(ZVM_SOURCES))) -o $(ZVM_BUILD)/host/test_zvm
	$(ZVM_BUILD)/host/test_zvm
	$(MAKE) zvmtest
	$(KOS_PYTHON) $(KOS_SDK_ROOT)/tools/exepack.py validate $(KOS_BUILD)/user/zvmtest.exe
test-cache:
	mkdir -p $(ZVM_BUILD)/host
	$(CC) $(ZVM_FLAGS) -std=c99 -Wall -Wextra -Werror -Itests/host/include -I$(KOS_SDK_ROOT)/include/abi -I$(KOS_SDK_ROOT)/include -Iinclude -Itests tests/test_story_cache.c tests/story_cache.c $(KOS_READER_FULL_SOURCE) -o $(ZVM_BUILD)/host/test_story_cache
	$(ZVM_BUILD)/host/test_story_cache
run-zork: zvmtest
	$(KOS_PYTHON) $(KOS_SDK_ROOT)/tools/exerun.py --exe $(KOS_BUILD)/user/zvmtest.exe --map $(KOS_BUILD)/user/zvmtest/zvmtest.map --card $(ZORK_CARD) --rundir $(KOS_BUILD)/run-zork --mame "$(KOS_MAME)" --flash "$(KOS_FLASH)" --plugins "$(KOS_MAME_PLUGINS)" $(ZORK_ASSETS) --args "-p /fs/ZORK1.Z3"
test-zork: zvmtest
	$(KOS_PYTHON) $(KOS_SDK_ROOT)/tools/exerun.py --exe $(KOS_BUILD)/user/zvmtest.exe --map $(KOS_BUILD)/user/zvmtest/zvmtest.map --card $(ZORK_CARD) --rundir $(KOS_BUILD)/run-zork-test --mame "$(KOS_MAME)" --mame-args='$(ZVM_MAME_ARGS)' --flash "$(KOS_FLASH)" --plugins "$(KOS_MAME_PLUGINS)" $(ZORK_ASSETS) --asset tests/zorkauto.flag=ZORKAUTO.TXT --args "-a /fs/ZORK1.Z3" --exit --timeout 300
clean:
	rm -rf build

# Run the unchanged baseline with this same harness, then the optimized source
# with identical ZVM_PROFILE/BENCH_MODE settings. Profile timings carry overhead.
BENCH_TIMEOUT ?= 900
BENCH_MODE ?= b
ZVM_MAME_ARGS ?= -nothrottle -autoboot_script $(abspath tests/m35_finish.lua) -autoboot_delay 0
.PHONY: bench bench-discard bench-both bench-profile
bench: zvmtest
	$(KOS_PYTHON) $(KOS_SDK_ROOT)/tools/exerun.py --exe $(KOS_BUILD)/user/zvmtest.exe --map $(KOS_BUILD)/user/zvmtest/zvmtest.map --card $(ZORK_CARD) --rundir $(KOS_BUILD)/run-bench-$(BENCH_MODE) --mame "$(KOS_MAME)" --mame-args='$(ZVM_MAME_ARGS)' --flash "$(KOS_FLASH)" --plugins "$(KOS_MAME_PLUGINS)" $(ZORK_ASSETS) --args "-$(BENCH_MODE) /fs/ZORK1.Z3" --exit --timeout $(BENCH_TIMEOUT)
bench-discard:
	$(MAKE) bench BENCH_MODE=d
bench-both:
	$(MAKE) bench BENCH_MODE=B
bench-profile:
	$(MAKE) bench ZVM_PROFILE=1

# ZIL state reporting uses the existing ZILf/ZAPF binaries and SDK build rules.
.PHONY: story story-baseline run-state-zork test-state-reporting
story: release
	$(KOS_PYTHON) ../tools/state-reporting/build_story.py
	cp ../build/state-reporting/instrumented/ZORK1.Z3 build/ZORK1.Z3
	cp ../build/state-reporting/instrumented/ZORK1.WOH build/ZORK1.WOH
	cp ../build/state-reporting/instrumented/ZORK1.bindings.json build/ZORK1.bindings.json
	cd build && sha256sum ZVMTEST.EXE ZORK1.Z3 ZORK1.WOH > STATE-SHA256SUMS
	@printf '%s\n' 'State-reporting delivery: build/ZVMTEST.EXE + build/ZORK1.Z3 + build/ZORK1.WOH'

story-baseline:
	$(KOS_PYTHON) ../tools/state-reporting/build_story.py --baseline

run-state-zork: story
	$(MAKE) run-zork ZORK_STORY=build/ZORK1.Z3 ZORK_BINDINGS=build/ZORK1.WOH ZVM_PROFILE=0 ZVM_ASM=0 ZVM_CACHE_ASM=1 ZVM_STACK_PROBE=0 ZVM_STACK=512 DYN_CACHE_PAGES=4 STATIC_CACHE_PAGES=4

test-state-reporting:
	cd .. && $(KOS_PYTHON) -m unittest discover -s tools/state-reporting -v
