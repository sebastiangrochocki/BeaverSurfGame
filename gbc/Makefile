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

RULES := src/rules.c src/wave.c src/save.c
GAME := src/main.c src/render.c src/sound.c
GEN := $(BUILD)/gen
ASSETS := $(GEN)/bg.c $(GEN)/bg0.c $(GEN)/beaver.c $(GEN)/board.c $(GEN)/gull.c $(GEN)/ring.c $(GEN)/heart.c $(GEN)/splash.c $(GEN)/arrow.c
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
	$(PNG2ASSET) $< -o $@ -spr8x16 -sw 24 -sh 32 -px 12 -py 12 -sp 0x02 $(PNGFLAGS)

$(GEN)/heart.c: res/heart.png | $(GEN)
	$(PNG2ASSET) $< -o $@ -spr8x16 -sw 16 -sh 16 -px 8 -py 8 -sp 0x03 $(PNGFLAGS)

$(GEN)/splash.c: res/splash.png | $(GEN)
	$(PNG2ASSET) $< -o $@ -spr8x16 -sw 32 -sh 16 -px 16 -py 16 -sp 0x04 $(PNGFLAGS)

$(GEN)/arrow.c: res/arrow.png | $(GEN)
	$(PNG2ASSET) $< -o $@ -spr8x16 -sw 8 -sh 16 -px 4 -py 0 -sp 0x05 $(PNGFLAGS)

$(BUILD)/beaver.gbc: $(RULES) $(GAME) $(ASSETS) src/rules.h src/render.h src/sound.h src/tiles.h | $(BUILD)
	$(LCC) $(CFLAGS) -o $@ $(RULES) $(GAME) $(ASSETS)

$(WEB)/beaver.gbc: $(BUILD)/beaver.gbc
	cp $< $@

probe: $(BUILD)/probe.gbc

$(BUILD)/probe.gbc: $(RULES) probe/main.c src/rules.h | $(BUILD)
	$(LCC) $(CFLAGS) -o $@ $(RULES) probe/main.c

clean:
	rm -rf $(BUILD)
