# Beaver Surf for Game Boy Color. Needs GBDK-2020 4.5.0 in $(GBDK) (default ~/gbdk).
#
#   make            -> build/beaver.gbc and ../web/beaver.gbc
#   make probe      -> build/probe.gbc, a frame-budget probe that times world_step on hardware
#   make clean

GBDK ?= $(HOME)/gbdk
LCC := $(GBDK)/bin/lcc
PNG2ASSET := $(GBDK)/bin/png2asset

BUILD := build
WEB := ../web

# Game Boy Color only, MBC5 + 8 KB battery RAM, 1 RAM bank, 8 ROM banks, speed-optimised code.
CART_FLAGS := -Wm-yC -Wm-yt0x1B -Wm-ya1 -Wm-yo8 -Wm-yn"BEAVERSURF"
CFLAGS := -Wf--opt-code-speed -Wf-Isrc -Wf-I$(BUILD)/gen -Wl-j $(CART_FLAGS)

RULES := src/rules.c src/wave.c src/save.c src/shop.c src/surf.c src/race.c
GAME := src/main.c src/render.c src/sprites.c src/sound.c src/beach.c src/actors.c src/shop_render.c
GEN := $(BUILD)/gen
ASSETS := $(GEN)/bg.c $(GEN)/bg0.c $(GEN)/beaver.c $(GEN)/board.c $(GEN)/gull.c $(GEN)/ring.c $(GEN)/heart.c $(GEN)/splash.c $(GEN)/arrow.c $(GEN)/pop.c
OBJECTS := $(addprefix $(BUILD)/obj/,$(patsubst %.c,%.o,$(RULES) $(GAME) $(ASSETS)))
PNGFLAGS := -keep_palette_order -repair_indexed_pal -noflip -bpp 2 -sprite_no_optimize -b 3

.PHONY: all probe clean

all: $(WEB)/beaver.gbc

$(BUILD) $(GEN):
	mkdir -p $(GEN)

$(GEN)/bg.c: res/bg.png | $(GEN)
	$(PNG2ASSET) $< -o $@ -map -use_map_attributes -keep_duplicate_tiles -keep_palette_order -repair_indexed_pal -noflip -bpp 2 -max_palettes 8 -b 3

$(GEN)/bg0.c: res/bg0.png | $(GEN)
	$(PNG2ASSET) $< -o $@ -map -use_map_attributes -keep_duplicate_tiles -keep_palette_order -repair_indexed_pal -noflip -bpp 2 -max_palettes 8 -b 3

$(GEN)/beaver.c: res/beaver.png | $(GEN)
	$(PNG2ASSET) $< -o $@ -spr8x16 -sw 32 -sh 32 -px 16 -py 32 -sp 0x00 $(PNGFLAGS)

$(GEN)/board.c: res/board.png | $(GEN)
	$(PNG2ASSET) $< -o $@ -spr8x16 -sw 32 -sh 16 -px 16 -py 0 -sp 0x00 $(PNGFLAGS)

$(GEN)/gull.c: res/gull.png | $(GEN)
	$(PNG2ASSET) $< -o $@ -spr8x16 -sw 32 -sh 16 -px 16 -py 8 -sp 0x01 $(PNGFLAGS)

$(GEN)/ring.c: res/ring.png | $(GEN)
	$(PNG2ASSET) $< -o $@ -spr8x16 -sw 16 -sh 16 -px 8 -py 8 -sp 0x02 $(PNGFLAGS)

$(GEN)/heart.c: res/heart.png | $(GEN)
	$(PNG2ASSET) $< -o $@ -spr8x16 -sw 16 -sh 16 -px 8 -py 8 -sp 0x03 $(PNGFLAGS)

$(GEN)/splash.c: res/splash.png | $(GEN)
	$(PNG2ASSET) $< -o $@ -spr8x16 -sw 32 -sh 16 -px 16 -py 16 -sp 0x04 $(PNGFLAGS)

$(GEN)/arrow.c: res/arrow.png | $(GEN)
	$(PNG2ASSET) $< -o $@ -spr8x16 -sw 8 -sh 16 -px 4 -py 0 -sp 0x05 $(PNGFLAGS)

$(GEN)/pop.c: res/pop.png | $(GEN)
	$(PNG2ASSET) $< -o $@ -spr8x16 -sw 16 -sh 16 -px 8 -py 8 -sp 0x02 $(PNGFLAGS)

src/shop_data.h: ../tools/gen_shop_assets.py art/surf.png src/shop.h
	python3 ../tools/gen_shop_assets.py

src/beach_data.h: res/beach/sand.png res/beach/water.png res/beach/shoreline.png art/walk_0.png art/walk_1.png art/walk_front.png art/walk_front_1.png art/walk_back.png art/walk_back_1.png ../tools/gen_beach_assets.py
	python3 ../tools/gen_beach_assets.py

src/actor_data.h: res/beaver.png res/board.png res/gull.png res/heart.png res/ring.png art/surf.png art/tug.png art/jump.png art/left.png art/right.png ../tools/gen_actor_runtime.py
	python3 ../tools/gen_actor_runtime.py

src/progress_data.h: art/progress_start.png art/progress_finish.png res/bg.png ../tools/gen_progress_assets.py
	python3 ../tools/gen_progress_assets.py

$(BUILD)/obj/%.o: %.c $(wildcard src/*.h) | $(GEN)
	mkdir -p $(@D)
	$(LCC) $(CFLAGS) -c -o $@ $<

$(filter $(BUILD)/obj/src/%,$(OBJECTS)): $(ASSETS)

$(BUILD)/beaver.gbc: $(OBJECTS) | $(BUILD)
	$(LCC) $(CFLAGS) -o $@ $(OBJECTS)

$(WEB)/beaver.gbc: $(BUILD)/beaver.gbc
	cp $< $@

probe: $(BUILD)/probe.gbc

$(BUILD)/probe.gbc: $(RULES) src/actors.c probe/main.c src/rules.h src/actors.h src/actor_data.h | $(BUILD)
	$(LCC) $(CFLAGS) -o $@ $(RULES) src/actors.c probe/main.c

clean:
	rm -rf $(BUILD)
