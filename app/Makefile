# G0 application only. The installed SDK, OS and libzvm core are not edited.
.DEFAULT_GOAL := all
KOS_SDK_ROOT ?= ../../ZephyrOS/build/kos-sdk-0.2.0
KOS_BUILD := build/target
EXE_LIB_DIR := build/lib
STORY ?= ../libzvm/build/ZORK1.Z3
BINDINGS ?= ../libzvm/build/ZORK1.WOH
SCENE ?= ../docs/west_of_house_256x144.g7
SDCC ?= sdcc
SDAR ?= sdar
CC ?= cc
PYTHON ?= python3
KOS_READER_FULL_SOURCE ?= ../../ZephyrOS/user/sdk/src/reader_full.c
KOS_POINTER_MEMBERS_zvm := memory.rel text.rel services.rel
include $(KOS_SDK_ROOT)/mk/kos.mk
EXE_CFLAGS += -DDYN_CACHE_PAGES=4 -DSTATIC_CACHE_PAGES=4 -DZVM_CACHE_ASM=1
VPATH := src:../libzvm/tests

SOURCES := src/main.c src/game.c src/game_output.c src/render.c src/panel.c src/menu.c src/fixture.c ../libzvm/tests/story_cache.c ../libzvm/tests/story_cache_z80.c
$(eval $(call exe_program,woh,.,$(SOURCES),STACK=768,LIBS=zvm:gfx:tui,RESIDENT=game_output.c:story_cache.c:story_cache_z80.c,PLACE=libgfx.lib(gfx_mode.rel)=resident:fixture.c=home,INCLUDES=src:gfx:build/assets:../libzvm/include:../libzvm/tests:$(KOS_SDK_ROOT)/include/kos))

.PHONY: all bundle test clean FORCE
all: bundle
build/assets/inputs.json build/assets/woh_bindings.h build/assets/scene-a.bin build/assets/scene-b.bin &: tools/prepare.py $(STORY) $(BINDINGS) $(SCENE)
	$(PYTHON) tools/prepare.py --story $(STORY) --metadata $(BINDINGS) --image "$(SCENE)" --output build/assets
$(woh_COBJS): Makefile $(wildcard src/*.h) gfx/gfx_border.h build/assets/woh_bindings.h

build/lib/libzvm.lib: FORCE
	$(MAKE) -C ../libzvm release KOS_SDK_ROOT=$(abspath $(KOS_SDK_ROOT))
	mkdir -p $(@D)
	cp ../libzvm/build/libzvm.lib $@
build/lib/libtui.lib: $(KOS_SDK_ROOT)/lib/libtui.lib
	mkdir -p $(@D)
	cp $< $@
build/gfx_border.rel: gfx/gfx_border.c gfx/gfx_border.h Makefile
	mkdir -p $(@D)
	$(SDCC) -mz80 -c --nostdinc --Werror -I$(KOS_SDK_ROOT)/include/abi -I$(KOS_SDK_ROOT)/include -Igfx $< -o $@
build/lib/libgfx.lib: $(KOS_SDK_ROOT)/lib/libgfx.lib build/gfx_border.rel tools/extend_gfx.py
	$(PYTHON) tools/extend_gfx.py --sdk $(KOS_SDK_ROOT) --extra build/gfx_border.rel --output $@ --sdar $(SDAR)

bundle: $(woh_EXE) build/assets/scene-a.bin build/assets/scene-b.bin tools/bundle.py
	mkdir -p build/WestOfHouse
	$(PYTHON) tools/bundle.py --sdk $(KOS_SDK_ROOT) --manifest build/target/user/woh/woh.json --assets build/assets --output build/WestOfHouse/woh.exe
	cp $(STORY) build/WestOfHouse/zork1.z3
	cd build/WestOfHouse && sha256sum woh.exe zork1.z3 > SHA256SUMS
	@printf '%s\n' 'G0 bundle: $(abspath build/WestOfHouse/woh.exe)' '           $(abspath build/WestOfHouse/zork1.z3)'

test: build/assets/woh_bindings.h
	mkdir -p build/host
	$(CC) -std=c99 -Wall -Wextra -Werror -I$(KOS_SDK_ROOT)/include/abi -Isrc tests/test_panel.c src/panel.c src/menu.c src/fixture.c -o build/host/test_panel
	build/host/test_panel
	$(CC) -std=c99 -Wall -Wextra -Werror -Itests/host -I../libzvm/tests/host/include -I$(KOS_SDK_ROOT)/include/abi -I$(KOS_SDK_ROOT)/include -Isrc -Igfx tests/test_render.c src/render.c src/panel.c src/menu.c src/fixture.c -o build/host/test_render
	build/host/test_render "$(SCENE)"
	$(CC) -std=c99 -Wall -Wextra -Werror -Itests/host -I../libzvm/tests/host/include -I$(KOS_SDK_ROOT)/include/abi -I$(KOS_SDK_ROOT)/include -Isrc -Ibuild/assets -I../libzvm/include -I../libzvm/tests tests/test_game.c src/game.c src/game_output.c src/panel.c ../libzvm/tests/story_cache.c $(KOS_READER_FULL_SOURCE) $(wildcard ../libzvm/src/*.c) -o build/host/test_game
	build/host/test_game "$(STORY)"
	WOH_SDK_ROOT=$(abspath $(KOS_SDK_ROOT)) $(PYTHON) -m unittest discover -s tests -p 'test_*.py' -v

clean:
	rm -rf build
