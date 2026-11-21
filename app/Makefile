# WOH application and explicit KRES resource package.
.DEFAULT_GOAL := all
KOS_SDK_ROOT ?= ../../ZephyrOS/build/kos-sdk-0.2.0
KOS_BUILD := build/target
EXE_LIB_DIR := build/lib
STORY ?= ../libzvm/build/ZORK1.Z3
BINDINGS ?= ../libzvm/build/ZORK1.WOH
SDCC ?= sdcc
SDAR ?= sdar
CC ?= cc
PYTHON ?= python3
KOS_READER_FULL_SOURCE ?= ../../ZephyrOS/user/sdk/src/reader_full.c
KOS_POINTER_MEMBERS_zvm := memory.rel text.rel services.rel
include $(KOS_SDK_ROOT)/mk/kos.mk
EXE_CFLAGS += -DDYN_CACHE_PAGES=4 -DSTATIC_CACHE_PAGES=4 -DZVM_CACHE_ASM=1
# Diagnostic builds: WOH_PROFILE=1 times each turn (PROF.BIN); ZVM_PROFILE=1
# adds the cache and VM counters (libzvm's profile variant).
ifeq ($(WOH_PROFILE),1)
EXE_CFLAGS += -DWOH_PROFILE=1
endif
ifeq ($(ZVM_PROFILE),1)
EXE_CFLAGS += -DZVM_PROFILE=1
ZVM_LIB_SOURCE := ../libzvm/build/variants/cache-4-4/profile-s512-fast/lib/libzvm.lib
endif
VPATH := src:../libzvm/tests:../tools/scene-hotspots

SOURCES := src/main.c src/game.c src/game_output.c src/render.c src/panel.c src/menu.c src/resources.c ../tools/scene-hotspots/consumer.c ../libzvm/tests/story_cache.c ../libzvm/tests/story_cache_setup.c ../libzvm/tests/story_cache_l2.c ../libzvm/tests/story_cache_z80.c
$(eval $(call exe_program,woh,.,$(SOURCES),STACK=768,LIBS=zvm:gfx:kres:snd,RESIDENT=game_output.c:story_cache.c:story_cache_z80.c,PLACE=libgfx.lib(gfx_mode.rel)=resident,INCLUDES=src:gfx:build/assets:../tools/scene-hotspots:../libzvm/include:../libzvm/tests:$(KOS_SDK_ROOT)/include/kos))

.PHONY: all bundle resources test clean FORCE
all: bundle
# The resource builder hashes inputs and writes only changed payloads/headers.
resources:
	$(PYTHON) ../tools/resources/build.py --sdk $(abspath $(KOS_SDK_ROOT))
build/assets/resource_names.h: resources
build/assets/inputs.json build/assets/woh_bindings.h &: tools/prepare.py $(STORY) $(BINDINGS)
	$(PYTHON) tools/prepare.py --story $(STORY) --metadata $(BINDINGS) --output build/assets
$(woh_COBJS): Makefile $(wildcard src/*.h) gfx/gfx_border.h build/assets/woh_bindings.h build/assets/resource_names.h

build/lib/libzvm.lib: FORCE
	$(MAKE) -C ../libzvm release KOS_SDK_ROOT=$(abspath $(KOS_SDK_ROOT))
	mkdir -p $(@D)
	cp $(or $(ZVM_LIB_SOURCE),../libzvm/build/libzvm.lib) $@
build/lib/libkres.lib: $(KOS_SDK_ROOT)/lib/libkres.lib
	mkdir -p $(@D)
	cp $< $@
build/lib/libsnd.lib: $(KOS_SDK_ROOT)/lib/libsnd.lib
	mkdir -p $(@D)
	cp $< $@
build/gfx_border.rel: gfx/gfx_border.c gfx/gfx_border.h Makefile
	mkdir -p $(@D)
	$(SDCC) -mz80 -c --nostdinc --Werror -I$(KOS_SDK_ROOT)/include/abi -I$(KOS_SDK_ROOT)/include -Igfx $< -o $@
build/lib/libgfx.lib: $(KOS_SDK_ROOT)/lib/libgfx.lib build/gfx_border.rel tools/extend_gfx.py
	$(PYTHON) tools/extend_gfx.py --sdk $(KOS_SDK_ROOT) --extra build/gfx_border.rel --output $@ --sdar $(SDAR)

bundle: $(woh_EXE) resources
	mkdir -p build/WestOfHouse
	cp $(woh_EXE) build/WestOfHouse/woh.exe
	rm -f build/WestOfHouse/zork1.z3
	cd build/WestOfHouse && sha256sum woh.exe WOH.KPR > SHA256SUMS
	@printf '%s\n' 'Deploy: $(abspath build/WestOfHouse/woh.exe)' '        $(abspath build/WestOfHouse/WOH.KPR)'

test: resources build/assets/woh_bindings.h
	mkdir -p build/host
	$(CC) -std=c99 -Wall -Wextra -Werror -I$(KOS_SDK_ROOT)/include/abi -Isrc tests/test_panel.c src/panel.c src/menu.c tests/fixture.c -o build/host/test_panel
	build/host/test_panel
	$(CC) -std=c99 -Wall -Wextra -Werror -Itests/host -I../libzvm/tests/host/include -I$(KOS_SDK_ROOT)/include/abi -I$(KOS_SDK_ROOT)/include -Isrc -Igfx tests/test_render.c src/render.c src/panel.c src/menu.c tests/fixture.c -o build/host/test_render
	build/host/test_render ../assets/runtime/scenes/room-180-state-1.img
	$(CC) -std=c99 -Wall -Wextra -Werror -Itests/host -I../libzvm/tests/host/include -I$(KOS_SDK_ROOT)/include/abi -I$(KOS_SDK_ROOT)/include -Isrc -Ibuild/assets -I../libzvm/include -I../libzvm/tests tests/test_game.c src/game.c src/game_output.c src/panel.c ../libzvm/tests/story_cache.c ../libzvm/tests/story_cache_setup.c ../libzvm/tests/story_cache_l2.c $(KOS_READER_FULL_SOURCE) $(wildcard ../libzvm/src/*.c) -o build/host/test_game
	build/host/test_game "$(STORY)"
	$(CC) -std=c99 -Wall -Wextra -Werror -Itests/host -I../libzvm/tests/host/include -I$(KOS_SDK_ROOT)/include/abi -I$(KOS_SDK_ROOT)/include -Isrc -Ibuild/assets -I../tools/scene-hotspots tests/test_resources.c src/resources.c src/menu.c ../tools/scene-hotspots/consumer.c ../../ZephyrOS/user/sdk/kres/src/kres_read.c ../../ZephyrOS/user/sdk/kres/src/kres_stream.c ../../ZephyrOS/user/sdk/kres/src/kres_state.c ../../ZephyrOS/user/sdk/kres/src/kres_util.c ../../ZephyrOS/user/sdk/kres/src/kres_reader.c $(KOS_READER_FULL_SOURCE) -o build/host/test_resources
	build/host/test_resources build/WestOfHouse/WOH.KPR ../assets/runtime/scenes/room-180-state-1.img ../assets/runtime/ui/WoHTitle.img
	WOH_SDK_ROOT=$(abspath $(KOS_SDK_ROOT)) $(PYTHON) -m unittest discover -s tests -p 'test_*.py' -v

clean:
	rm -rf build
