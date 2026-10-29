//! Game Boy Color art. Every 8x8 tile uses one 4-colour palette, and each class stays within
//! the eight palettes the hardware allows. `png2asset` turns the indexed PNGs into C arrays.

use std::collections::HashMap;
use std::fs;
use std::path::Path;

use crate::glyph;

const TRANS: u8 = 0xFF;

/// Background palettes, in the order png2asset keeps (`-keep_palette_order`).
/// Colour 0 of cloud and sun matches sky colour 2, and colour 0 of island and ship is the
/// same far-sea blue, so those props sit on the background without a rectangular halo.
const BG: [[[u8; 3]; 4]; 8] = [
    [[8, 32, 72], [255, 255, 255], [220, 48, 56], [64, 192, 88]], // HUD
    [
        [16, 52, 128],
        [40, 108, 188],
        [96, 176, 224],
        [176, 216, 240],
    ], // sky
    [
        [96, 176, 224],
        [255, 255, 255],
        [140, 168, 188],
        [176, 216, 240],
    ], // cloud
    [
        [18, 77, 114],
        [56, 168, 64],
        [220, 184, 112],
        [72, 164, 208],
    ], // island / far sea
    [[18, 77, 114], [255, 255, 255], [128, 72, 36], [208, 40, 48]], // ship
    [
        [96, 176, 224],
        [255, 184, 24],
        [255, 228, 128],
        [255, 255, 255],
    ], // sun
    [[18, 77, 114], [36, 125, 163], [101, 184, 204], [228, 245, 237]], // water
    [
        [18, 77, 114],
        [228, 245, 237],
        [101, 184, 204],
        [36, 125, 163],
    ], // waterline: colour 0 is the far-sea blue, so the wave rises out of the sea behind it
];

/// Sprite palettes. Colour 0 is transparent. `sprite_pals` in render.c repeats these in RGB555.
const SP: [[[u8; 3]; 4]; 6] = [
    [[0, 0, 0], [0, 0, 0], [156, 90, 58], [156, 0, 49]],       // supplied beaver/board: ink, fur, maroon
    [[0, 0, 0], [48, 40, 32], [255, 255, 255], [255, 160, 32]],  // gull
    [[0, 0, 0], [140, 80, 8], [248, 188, 32], [255, 236, 140]], // ring
    [[0, 0, 0], [120, 16, 24], [232, 48, 56], [255, 255, 255]], // heart
    [[0, 0, 0], [16, 80, 150], [180, 220, 240], [255, 255, 255]], // splash
    [[0, 0, 0], [8, 32, 72], [232, 48, 56], [255, 255, 255]],   // arrow
];

const BASE_TILES: usize = 70;
/// Ramps fill VRAM bank 1 above the base tiles first; the rest go to bank 0 above the sprite
/// patterns (render.c checks the sprite sheets really end at BANK0_RAMP_BASE).
const BANK1_RAMP_MAX: usize = 256 - BASE_TILES;
const BANK0_RAMP_BASE: usize = 194;
const BANK0_RAMP_MAX: usize = 256 - BANK0_RAMP_BASE;
/// Eleven pixels per column covers the steepest faces; twelve would cost 24 more tiles for a
/// one-pixel difference and bank 0 is shared with the sprite sheets.
const RAMP_DY_MIN: i32 = -11;
const RAMP_DY_MAX: i32 = 11;

/// Seven representative slopes share the fixed tile budget with the complete depth profile.
fn ramp_slope(dy: i32) -> i32 {
    let magnitude = match dy.abs() {
        0..=1 => 0,
        2..=3 => 2,
        4..=5 => 4,
        _ => 8,
    };
    magnitude * dy.signum()
}
const RAMP_SUB_MIN: i32 = -32;
const RAMP_SUB_MAX: i32 = 15;

const HUD: u8 = 0;
const SKY: u8 = 1;
const CLOUD: u8 = 2;
const ISLAND: u8 = 3;
const SHIP: u8 = 4;
const SUN: u8 = 5;
const WATER: u8 = 6;
const LINE: u8 = 7;

struct Img {
    width: usize,
    height: usize,
    /// `0xFF` transparent, otherwise `(palette << 2) | colour`.
    pixels: Vec<u8>,
    sprite: bool,
}

impl Img {
    fn new(width: usize, height: usize, sprite: bool) -> Self {
        Self {
            width,
            height,
            pixels: vec![if sprite { TRANS } else { 0 }; width * height],
            sprite,
        }
    }

    fn set(&mut self, x: i32, y: i32, pal: u8, col: u8) {
        if x >= 0 && y >= 0 && (x as usize) < self.width && (y as usize) < self.height && col < 4 {
            self.pixels[y as usize * self.width + x as usize] = (pal << 2) | col;
        }
    }

    fn fill(&mut self, x: i32, y: i32, w: i32, h: i32, pal: u8, col: u8) {
        for py in y..y + h {
            for px in x..x + w {
                self.set(px, py, pal, col);
            }
        }
    }

    fn oval(&mut self, cx: i32, cy: i32, rx: i32, ry: i32, pal: u8, col: u8) {
        for y in cy - ry..=cy + ry {
            for x in cx - rx..=cx + rx {
                let dx = (x - cx) * ry;
                let dy = (y - cy) * rx;
                if dx * dx + dy * dy <= rx * rx * ry * ry {
                    self.set(x, y, pal, col);
                }
            }
        }
    }

    fn blit_tile(&mut self, index: usize, tile: &Img) {
        let ox = index * 8;
        for y in 0..8 {
            for x in 0..8 {
                let color = tile.pixels[y * 8 + x];
                if color != TRANS {
                    self.pixels[y * self.width + ox + x] = color;
                }
            }
        }
    }

    /// Copies a 16x16 block as four tiles: top-left, top-right, bottom-left, bottom-right.
    fn blit_block(&mut self, index: usize, block: &Img) {
        for row in 0..2 {
            for col in 0..2 {
                for y in 0..8 {
                    for x in 0..8 {
                        let color = block.pixels[(row * 8 + y) * block.width + col * 8 + x];
                        let dst = index + row * 2 + col;
                        self.pixels[y * self.width + dst * 8 + x] = color;
                    }
                }
            }
        }
    }

    fn check(&self, name: &str, max_palettes: usize) {
        assert_eq!(self.width % 8, 0, "{name} width");
        assert_eq!(self.height % 8, 0, "{name} height");
        let mut used = [false; 8];
        for ty in 0..self.height / 8 {
            for tx in 0..self.width / 8 {
                let mut pal = None;
                let mut colours = [false; 4];
                for y in 0..8 {
                    for x in 0..8 {
                        let pixel = self.pixels[(ty * 8 + y) * self.width + tx * 8 + x];
                        if pixel == TRANS {
                            assert!(self.sprite, "{name} tile {tx},{ty} has a hole");
                            continue;
                        }
                        let p = pixel >> 2;
                        let c = pixel & 3;
                        assert!(p < 8, "{name} palette {p}");
                        match pal {
                            None => pal = Some(p),
                            Some(prev) => assert_eq!(
                                prev, p,
                                "{name} tile {tx},{ty} mixes palettes {prev} and {p}"
                            ),
                        }
                        colours[c as usize] = true;
                        used[p as usize] = true;
                    }
                }
                let count = colours.iter().filter(|c| **c).count();
                assert!(count <= 4, "{name} tile {tx},{ty} has {count} colours");
            }
        }
        let palettes = used.iter().filter(|u| **u).count();
        assert!(palettes <= max_palettes, "{name} uses {palettes} palettes");
    }

    fn write_indexed(&self, path: &Path, source: &[[[u8; 3]; 4]]) {
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent).unwrap();
        }
        let mut palette = Vec::new();
        for pal in source {
            for color in pal {
                palette.extend_from_slice(color);
            }
        }
        let file = fs::File::create(path).unwrap();
        let mut encoder = png::Encoder::new(file, self.width as u32, self.height as u32);
        encoder.set_color(png::ColorType::Indexed);
        encoder.set_depth(png::BitDepth::Eight);
        encoder.set_palette(palette);
        if self.sprite {
            // Colour 0 of every sprite palette is transparent.
            let mut trns = vec![255u8; source.len() * 4];
            for pal in 0..source.len() {
                trns[pal * 4] = 0;
            }
            encoder.set_trns(trns);
        }
        let data: Vec<u8> = self
            .pixels
            .iter()
            .map(|pixel| if *pixel == TRANS { 0 } else { *pixel })
            .collect();
        encoder
            .write_header()
            .unwrap()
            .write_image_data(&data)
            .unwrap();
    }
}

pub fn write_pickups(root: &Path) {
    ring_sheet().write_indexed(&root.join("res/ring.png"), &SP[2..3]);
    pop_sheet().write_indexed(&root.join("res/pop.png"), &SP[2..3]);
}

pub fn write(root: &Path) {
    let res = root.join("res");
    let ramps = build_ramps();
    let bank1 = ramps.tiles.len().min(BANK1_RAMP_MAX);
    let tiles = tileset(&ramps.tiles[..bank1]);
    tiles.check("bg", 8);
    tiles.write_indexed(&res.join("bg.png"), &BG);
    // Bank 0 holds shared ramps that did not fit bank 1, followed by the scenery.
    let spill = ramps.tiles.len() - bank1;
    let scenery = scenery_tiles();
    let scene_base = BANK0_RAMP_BASE + spill;
    let bank0: Vec<&Img> = ramps.tiles[bank1..]
        .iter()
        .chain(scenery.iter())
        .collect();
    assert!(
        bank0.len() <= BANK0_RAMP_MAX,
        "bank 0 ramps and scenery overflow VRAM ({} > {})",
        bank0.len(),
        BANK0_RAMP_MAX
    );
    let tiles0 = tileset0(&bank0);
    tiles0.check("bg0", 8);
    tiles0.write_indexed(&res.join("bg0.png"), &BG);
    write_ramp_header(&root.join("src/ramp.h"), &ramps);

    write_tiles_header(&root.join("src/tiles.h"), ramps.tiles.len(), bank1, scene_base);

    write_actors(root);

    let gull = gull_sheet();
    gull.check("gull", 1);
    gull.write_indexed(&res.join("gull.png"), &SP[1..2]);

    let ring = ring_sheet();
    ring.check("ring", 1);
    ring.write_indexed(&res.join("ring.png"), &SP[2..3]);

    let heart = heart_sheet();
    heart.check("heart", 1);
    heart.write_indexed(&res.join("heart.png"), &SP[3..4]);

    let splash = splash_sheet();
    splash.check("splash", 1);
    splash.write_indexed(&res.join("splash.png"), &SP[4..5]);

    let pop = pop_sheet();
    pop.check("pop", 1);
    pop.write_indexed(&res.join("pop.png"), &SP[2..3]);

    let arrow = arrow_sheet();
    arrow.check("arrow", 1);
    arrow.write_indexed(&res.join("arrow.png"), &SP[5..6]);
}

fn solid(pal: u8, col: u8) -> Img {
    let mut tile = Img::new(8, 8, false);
    tile.fill(0, 0, 8, 8, pal, col);
    tile
}

fn tileset(ramps: &[Img]) -> Img {
    let mut sheet = Img::new((BASE_TILES + ramps.len()) * 8, 8, false);
    sheet.blit_tile(0, &solid(HUD, 0));
    sheet.blit_tile(1, &heart_tile(2));
    sheet.blit_tile(2, &heart_tile(1));
    sheet.blit_tile(3, &heart_tile(0));
    for digit in 0..10 {
        sheet.blit_tile(4 + digit, &glyph_tile(b'0' + digit as u8, HUD, 0, 1));
    }
    for letter in 0..26 {
        sheet.blit_tile(14 + letter, &glyph_tile(b'A' + letter as u8, HUD, 0, 1));
    }
    sheet.blit_tile(40, &meter_tile(false));
    sheet.blit_tile(41, &meter_tile(true));
    sheet.blit_tile(42, &sky_deep());
    sheet.blit_tile(43, &sky_haze());
    sheet.blit_tile(44, &solid(SKY, 2));
    sheet.blit_tile(45, &solid(SKY, 3));
    sheet.blit_block(46, &cloud_block());
    sheet.blit_block(50, &island_block());
    sheet.blit_block(54, &ship_block());
    sheet.blit_block(58, &sun_block());
    sheet.blit_tile(62, &far_tile(true));
    sheet.blit_tile(63, &far_tile(false));
    sheet.blit_tile(64, &face_tile());
    sheet.blit_tile(65, &body_tile());
    sheet.blit_tile(66, &deep_tile());
    sheet.blit_tile(67, &spray_tile(0));
    sheet.blit_tile(68, &spray_tile(1));
    sheet.blit_tile(69, &front_body_tile());
    for (index, tile) in ramps.iter().enumerate() {
        sheet.blit_tile(BASE_TILES + index, tile);
    }
    sheet
}

/// Bank-0 overflow sheet. Never empty, so png2asset always has a tile to emit.
fn tileset0(ramps: &[&Img]) -> Img {
    let mut sheet = Img::new(ramps.len().max(1) * 8, 8, false);
    sheet.blit_tile(0, &solid(LINE, 0));
    for (index, tile) in ramps.iter().enumerate() {
        sheet.blit_tile(index, tile);
    }
    sheet
}

struct Ramps {
    /// Shared wave ramps; ids index this list.
    tiles: Vec<Img>,
    lut: Vec<u8>,
}

/// Surface-relative bands shared by both waves. No noise or screen-row shading.
/// Every vertical pixel phase is represented; both lanes reuse the same patterns.
fn ramp_from_edges(sub: i32, dy: i32) -> Img {
    let mut tile = solid(WATER, 0);
    for x in 0..8i32 {
        let surface = sub + (dy * x).div_euclid(8);
        for y in 0..8i32 {
            let depth = y - surface;
            let col = match depth {
                0 => 3,
                1..=6 => 2,
                7..=22 => 1,
                _ => 0,
            };
            tile.set(x, y, WATER, col);
        }
    }
    tile
}

fn front_body_tile() -> Img {
    solid(WATER, 0)
}

/// Both lanes reuse exactly the same tile patterns, including the depth transitions.
fn build_ramps() -> Ramps {
    let mut tiles: Vec<Img> = Vec::new();
    let mut seen: HashMap<Vec<u8>, u8> = HashMap::new();
    let mut lut = Vec::new();
    for dy in RAMP_DY_MIN..=RAMP_DY_MAX {
        for sub in RAMP_SUB_MIN..=RAMP_SUB_MAX {
            let tile = ramp_from_edges(sub, ramp_slope(dy));
            let id = *seen.entry(tile.pixels.clone()).or_insert_with(|| {
                let id = u8::try_from(tiles.len()).expect("ramp ids overflow a byte");
                tiles.push(tile);
                id
            });
            lut.push(id);
        }
    }
    println!("gbc ramps: {} shared surface/depth tiles", tiles.len());
    Ramps { tiles, lut }
}

fn push_lut(body: &mut String, name: &str, lut: &[u8], row_len: usize) {
    body.push_str(&format!("static const uint8_t {}[{}] = {{\n", name, lut.len()));
    for chunk in lut.chunks(row_len) {
        body.push_str("    ");
        for (col, value) in chunk.iter().enumerate() {
            if col > 0 {
                body.push(' ');
            }
            body.push_str(&format!("0x{value:02X},"));
        }
        body.push('\n');
    }
    body.push_str("};\n\n");
}

fn write_ramp_header(path: &Path, ramps: &Ramps) {
    let mut body = String::from(
        "/* Generated by tools/gen_assets. Diagonal waterline tiles.\n\
         * RAMP_LUT[(dy - RAMP_DY_MIN) * RAMP_SUB_COUNT + (sub - RAMP_SUB_MIN)] is the shared\n\
         * ramp id for either wave (see tiles.h for the bank split). Includes surface-relative\n\
         * crest, bright face, body and deep-water bands. */\n\
         #ifndef BEAVER_RAMP_H\n\
         #define BEAVER_RAMP_H\n\
         #include <stdint.h>\n\n",
    );
    let sub_count = (RAMP_SUB_MAX - RAMP_SUB_MIN + 1) as usize;
    body.push_str(&format!(
        "#define RAMP_DY_MIN ({})\n#define RAMP_DY_MAX {}\n#define RAMP_SUB_MIN ({})\n#define RAMP_SUB_MAX {}\n#define RAMP_SUB_COUNT {}\n\n",
        RAMP_DY_MIN, RAMP_DY_MAX, RAMP_SUB_MIN, RAMP_SUB_MAX, sub_count
    ));
    push_lut(&mut body, "RAMP_LUT", &ramps.lut, sub_count);
    body.push_str("#endif\n");
    fs::write(path, body).unwrap();
}

fn glyph_tile(ch: u8, pal: u8, bg: u8, fg: u8) -> Img {
    let mut tile = solid(pal, bg);
    for (y, row) in glyph(ch).iter().enumerate() {
        for x in 0..8 {
            if row & (0x80 >> x) != 0 {
                tile.set(x, y as i32, pal, fg);
            }
        }
    }
    tile
}

fn heart_tile(halves: u8) -> Img {
    let mut tile = solid(HUD, 0);
    let art = [
        0b0110_0110,
        0b1111_1111,
        0b1111_1111,
        0b1111_1111,
        0b0111_1110,
        0b0011_1100,
        0b0001_1000,
        0b0000_0000,
    ];
    for (y, row) in art.iter().enumerate() {
        for x in 0..8 {
            if row & (0x80 >> x) == 0 {
                continue;
            }
            let on = match halves {
                2 => true,
                1 => x < 4,
                _ => false,
            };
            tile.set(x, y as i32, HUD, if on { 2 } else { 1 });
        }
    }
    tile
}

fn meter_tile(green: bool) -> Img {
    let mut tile = solid(HUD, 0);
    tile.fill(0, 2, 8, 4, HUD, if green { 3 } else { 1 });
    tile.set(0, 2, HUD, 0);
    tile.set(0, 5, HUD, 0);
    tile
}

fn sky_deep() -> Img {
    let mut tile = solid(SKY, 0);
    for y in 0..8i32 {
        for x in 0..8i32 {
            if y > 4 || (y > 2 && (x + y) % 2 == 0) {
                tile.set(x, y, SKY, 1);
            }
        }
    }
    tile
}

fn sky_haze() -> Img {
    let mut tile = solid(SKY, 3);
    for y in 0..8i32 {
        for x in 0..8i32 {
            if y < 3 || (y < 5 && (x + y) % 2 == 0) {
                tile.set(x, y, SKY, 2);
            }
        }
    }
    tile
}

fn cloud_block() -> Img {
    let mut img = Img::new(16, 16, false);
    let rows = [
        "AAAAAAAAAAAAAAAA",
        "AAAAAAAAAAAAAAAA",
        "AAAAAAAAAAAAAAAA",
        "AAAAAAAAAAAAAAAA",
        "AAAAAAAAAAAAAAAA",
        "AAAAAAAAAAAAAAAA",
        "AAAAAABBBBAAAAAA",
        "AAAABBBBBBBBAAAA",
        "AAABBBBBBBBBBAAA",
        "AACCBBBCCBBBCCAA",
        "ACCCCBCCCCBCCCCA",
        "AAACCCCCCCCCCAAA",
        "AAAAAAAAAAAAAAAA",
        "AAAAAAAAAAAAAAAA",
        "AAAAAAAAAAAAAAAA",
        "AAAAAAAAAAAAAAAA",
    ];
    for (y, row) in rows.iter().enumerate() {
        for (x, ch) in row.bytes().enumerate() {
            let colour = match ch {
                b'B' => 1,
                b'C' => 3,
                _ => 0,
            };
            img.set(x as i32, y as i32, CLOUD, colour);
        }
    }
    img
}

fn island_block() -> Img {
    let mut img = Img::new(16, 16, false);
    img.fill(0, 0, 16, 16, ISLAND, 0);
    img.oval(8, 12, 7, 3, ISLAND, 2);
    img.oval(8, 10, 6, 3, ISLAND, 1);
    img.oval(7, 9, 4, 2, ISLAND, 1);
    // Palm trunk and fronds.
    img.fill(10, 4, 1, 7, ISLAND, 2);
    img.set(9, 5, ISLAND, 1);
    img.set(8, 4, ISLAND, 1);
    img.set(11, 4, ISLAND, 1);
    img.set(12, 5, ISLAND, 1);
    img.set(7, 5, ISLAND, 1);
    img.set(13, 4, ISLAND, 1);
    img.set(9, 3, ISLAND, 1);
    img.set(11, 3, ISLAND, 1);
    img.set(10, 3, ISLAND, 1);
    for x in 2..14 {
        if x % 3 == 0 {
            img.set(x, 14, ISLAND, 3);
        }
    }
    img.set(3, 13, ISLAND, 3);
    img.set(13, 13, ISLAND, 3);
    img
}

fn ship_block() -> Img {
    let mut img = Img::new(16, 16, false);
    img.fill(0, 0, 16, 16, SHIP, 0);
    img.fill(2, 10, 12, 2, SHIP, 2);
    img.fill(4, 12, 8, 1, SHIP, 2);
    img.set(1, 10, SHIP, 2);
    img.set(14, 11, SHIP, 2);
    img.fill(7, 3, 1, 7, SHIP, 2);
    img.fill(8, 4, 4, 4, SHIP, 1);
    img.set(12, 5, SHIP, 2);
    img.set(8, 4, SHIP, 2);
    img.set(9, 6, SHIP, 0);
    img.fill(5, 2, 2, 2, SHIP, 3);
    img.set(6, 11, SHIP, 1);
    img.set(4, 13, SHIP, 1);
    img.set(12, 13, SHIP, 1);
    img.set(2, 11, SHIP, 3);
    img
}

fn sun_block() -> Img {
    let mut img = Img::new(16, 16, false);
    img.fill(0, 0, 16, 16, SUN, 0);
    for (x, y) in [
        (8, 1),
        (8, 14),
        (1, 8),
        (14, 8),
        (3, 3),
        (12, 3),
        (3, 12),
        (12, 12),
    ] {
        img.set(x, y, SUN, 2);
    }
    img.oval(8, 8, 5, 5, SUN, 1);
    img.oval(7, 7, 3, 3, SUN, 2);
    img.set(5, 5, SUN, 3);
    img.set(6, 5, SUN, 3);
    img.set(5, 6, SUN, 3);
    img
}

fn far_tile(top: bool) -> Img {
    let mut tile = solid(ISLAND, 0);
    if top {
        for x in 0..8i32 {
            let crest = if x % 4 == 0 { 2 } else { 3 };
            tile.set(x, crest, ISLAND, 3);
            if x % 5 == 1 {
                tile.set(x, 5, ISLAND, 3);
            }
        }
    } else {
        for x in 0..8i32 {
            if x % 4 == 2 {
                tile.set(x, 3, ISLAND, 3);
            }
        }
    }
    tile
}

// Flat fills are used only outside the slope/depth lookup range.
fn face_tile() -> Img {
    solid(WATER, 2)
}
fn body_tile() -> Img {
    solid(WATER, 1)
}
fn deep_tile() -> Img {
    solid(WATER, 0)
}

fn spray_tile(frame: usize) -> Img {
    let mut tile = solid(LINE, 0);
    let drops: [(i32, i32); 8] = [
        (1, 1),
        (3, 3),
        (5, 2),
        (6, 5),
        (2, 6),
        (4, 4),
        (0, 5),
        (7, 3),
    ];
    for (index, (x, y)) in drops.iter().enumerate() {
        if index % 2 == frame {
            tile.set(*x, *y, LINE, 1);
            tile.set(*x, *y + 1, LINE, 2);
        }
    }
    tile
}

/// Rebuild actor sheets without touching backgrounds, ramps, or scenery.
pub fn write_actors(root: &Path) {
    let res = root.join("res");
    let beaver = beaver_sheet(root);
    beaver.check("beaver", 1);
    beaver.write_indexed(&res.join("beaver.png"), &SP[0..1]);
    let board = board_sheet(root);
    board.check("board", 1);
    board.write_indexed(&res.join("board.png"), &SP[0..1]);
}

fn beaver_sheet(root: &Path) -> Img {
    let mut sheet = Img::new(32 * 7, 32, true);
    let art = root.join("art");
    let mut flip = load_pose(&art.join("jump.png"));
    let board = load_rgba(&art.join("board.png"), 32, 16);
    // Attach the overlapping deck before rotating the jump pose for a backflip.
    for y in 0..16 {
        for x in 0..32 {
            let source = (y * 32 + x) * 4;
            let dest = ((y + 16) * 32 + x) * 4;
            if flip[dest + 3] == 0 && board[source + 3] != 0 {
                flip[dest..dest + 4].copy_from_slice(&board[source..source + 4]);
            }
        }
    }
    let poses = [
        load_pose(&art.join("surf.png")),
        load_pose(&art.join("tug.png")),
        load_pose(&art.join("jump.png")),
        flip.clone(),
        rotate_ccw(&flip),
        load_pose(&art.join("left.png")),
        load_pose(&art.join("right.png")),
    ];
    for (frame, pose) in poses.iter().enumerate() {
        blit_pose(&mut sheet, frame, pose);
    }
    sheet
}

fn load_pose(path: &Path) -> Vec<u8> {
    load_rgba(path, 32, 32)
}

fn load_rgba(path: &Path, width: u32, height: u32) -> Vec<u8> {
    let file = fs::File::open(path).unwrap_or_else(|err| panic!("open {}: {err}", path.display()));
    let decoder = png::Decoder::new(file);
    let mut reader = decoder.read_info().unwrap();
    let mut buf = vec![0; reader.output_buffer_size()];
    let info = reader.next_frame(&mut buf).unwrap();
    assert_eq!((info.width, info.height), (width, height), "{}", path.display());
    assert_eq!(info.color_type, png::ColorType::Rgba, "{}", path.display());
    buf[..info.buffer_size()].to_vec()
}

/// Quarter turn counter-clockwise: a right-facing beaver rotates backwards.
fn rotate_ccw(src: &[u8]) -> Vec<u8> {
    let mut dst = vec![0u8; 32 * 32 * 4];
    for y in 0..32 {
        for x in 0..32 {
            let sx = 31 - y;
            let sy = x;
            let from = (sy * 32 + sx) * 4;
            let to = (y * 32 + x) * 4;
            dst[to..to + 4].copy_from_slice(&src[from..from + 4]);
        }
    }
    dst
}

fn blit_pose(sheet: &mut Img, frame: usize, rgba: &[u8]) {
    let ox = frame as i32 * 32;
    for y in 0..32 {
        for x in 0..32 {
            let i = (y * 32 + x) * 4;
            let pixel = &rgba[i..i + 4];
            if pixel[3] == 0 {
                continue;
            }
            let col = match (pixel[0], pixel[1], pixel[2]) {
                (0, 0, 0) => 1,
                (156, 90, 58) => 2,
                (156, 0, 49) => 3,
                other => panic!("beaver pixel {other:?} at {x},{y}"),
            };
            sheet.set(ox + x as i32, y as i32, 0, col);
        }
    }
}

/// Three 32x16 boards: flat, nose up, nose down. Flat stays under the cruise poses.
fn board_sheet(root: &Path) -> Img {
    let mut sheet = Img::new(32 * 3, 16, true);
    let art = root.join("art");
    for (frame, name) in ["board.png", "board_up.png", "board_down.png"].iter().enumerate() {
        let rgba = load_rgba(&art.join(name), 32, 16);
        let ox = frame as i32 * 32;
        for y in 0..16 {
            for x in 0..32 {
                let i = (y * 32 + x) * 4;
                let pixel = &rgba[i..i + 4];
                if pixel[3] == 0 {
                    continue;
                }
                let col = match (pixel[0], pixel[1], pixel[2]) {
                    (0, 0, 0) => 1,
                    (156, 0, 49) => 3,
                    other => panic!("board pixel {other:?} at {x},{y}"),
                };
                // Preserve every row of the supplied 32x16 canvas; tilted noses can
                // extend to row zero. Do not crop or shift the artwork inside the frame.
                // The renderer overlaps this canvas with the body's lower 16 rows.
                sheet.set(ox + x as i32, y as i32, 0, col);
            }
        }
    }
    sheet
}


/// Turns fur pixels that touch transparency into the dark outline.
fn add_outline(img: &mut Img) {
    let w = img.width;
    let src = img.pixels.clone();
    for y in 0..img.height {
        for x in 0..w {
            if src[y * w + x] == TRANS {
                continue;
            }
            let mut open = false;
            for (dx, dy) in [(-1, 0), (1, 0), (0, -1), (0, 1)] {
                let nx = x as i32 + dx;
                let ny = y as i32 + dy;
                if nx < 0
                    || ny < 0
                    || nx >= w as i32
                    || ny >= img.height as i32
                    || src[ny as usize * w + nx as usize] == TRANS
                {
                    open = true;
                }
            }
            if open {
                img.pixels[y * w + x] = 1;
            }
        }
    }
}

fn gull_sheet() -> Img {
    let mut sheet = Img::new(96, 16, true);
    for (frame, art) in [GULL_UP, GULL_MID, GULL_DOWN].iter().enumerate() {
        paint_gull(&mut sheet, frame as i32 * 32, art);
    }
    sheet
}

/// Side view flying left: orange beak, eye, white body and tail, and the near wing on its up,
/// level and down strokes with dark tips. Each frame is 32x16; the body never moves so the
/// stroke alone animates.
const GULL_UP: &str = "
.......................oooo.....
.....................oooooo.....
..................ooofffo.......
.................offfffo........
...............oofffffo.........
.....oooo.....offffffo..........
....offffo..ooffffffo...........
...offffffoofffffffo............
bbbofoffffffffffffffooooo.......
....oooofffffffffffffffffooo....
........offfffffffffffffffooo...
.........offfffffffffffooo......
..........ooooooooooooo.........
................................
................................
................................
";

const GULL_MID: &str = "
................................
................................
................................
................................
....................oooooooooo..
.....oooo......ooooofffffffooo..
....offffo..oooffffffffooooo....
...offffffooffffffffffo.........
bbbofofffffffffffffffffoo.......
....oooofffffffffffffffffooo....
........offfffffffffffffffooo...
.........offfffffffffffooo......
..........ooooooooooooo.........
................................
................................
................................
";

const GULL_DOWN: &str = "
................................
................................
................................
................................
................................
.....oooo.......................
....offffo......................
...offffffoooooooooo............
bbbofoffffffffffffffooooo.......
....oooofffffffffffffffffooo....
........offfffffffffffffffooo...
.........offfffffffffffooo......
..........ooooffffffffo.........
..............ooffffffo.........
................ooofffoo........
...................oooooo.......
";

fn paint_gull(img: &mut Img, ox: i32, art: &str) {
    let rows: Vec<&str> = art.lines().filter(|line| !line.is_empty()).collect();
    assert_eq!(rows.len(), 16, "gull height");
    for (y, row) in rows.iter().enumerate() {
        assert_eq!(row.chars().count(), 32, "gull width line {y}");
        for (x, ch) in row.chars().enumerate() {
            let col = match ch {
                'o' => 1,
                'f' => 2,
                'b' => 3,
                '.' => continue,
                other => panic!("bad gull pixel {other}"),
            };
            img.set(ox + x as i32, y as i32, 0, col);
        }
    }
}

fn ring_sheet() -> Img {
    let mut img = Img::new(16, 16, true);
    // A small readable gold hoop: dark rim, bright upper edge, transparent opening.
    stamp_hoop(&mut img, 0, 7, 7, 7, 4);
    img.set(4, 3, 0, 3);
    img.set(5, 2, 0, 3);
    img.set(4, 4, 0, 3);
    img
}

fn heart_sheet() -> Img {
    let mut img = Img::new(16, 16, true);
    img.oval(5, 5, 3, 3, 0, 2);
    img.oval(10, 5, 3, 3, 0, 2);
    img.fill(2, 6, 12, 3, 0, 2);
    for row in 0..6 {
        let half = 5 - row;
        img.fill(8 - half, 9 + row, half * 2, 1, 0, 2);
    }
    add_outline(&mut img);
    img.oval(5, 4, 2, 2, 0, 2);
    img.oval(10, 4, 2, 2, 0, 2);
    img.set(4, 4, 0, 3);
    img.set(5, 3, 0, 3);
    img.set(9, 4, 0, 3);
    img
}

fn splash_sheet() -> Img {
    let mut sheet = Img::new(32, 16, true);
    paint_splash(&mut sheet, 0, true);
    sheet
}

/// Two 16x16 frames: the shrunken hoop, then the droplets it bursts into.
fn pop_sheet() -> Img {
    let mut img = Img::new(32, 16, true);
    stamp_hoop(&mut img, 0, 7, 7, 6, 3);
    let specks = [(2, 2), (12, 1), (1, 12), (13, 12), (7, 0), (8, 14), (0, 6), (15, 7)];
    for (i, (x, y)) in specks.iter().enumerate() {
        let col = if i % 2 == 0 { 3 } else { 2 };
        img.set(16 + x, *y, 0, col);
        if *y + 1 < 16 {
            img.set(16 + x, y + 1, 0, 1);
        }
    }
    img
}

fn stamp_hoop(img: &mut Img, ox: i32, cx: i32, cy: i32, outer: i32, inner: i32) {
    for y in 0..16 {
        for x in 0..16 {
            let dx = x - cx;
            let dy = y - cy;
            let d2 = dx * dx + dy * dy;
            if d2 < inner * inner || d2 > outer * outer {
                continue;
            }
            let col = if d2 > (outer - 1) * (outer - 1) {
                1
            } else if dy < -1 {
                3
            } else {
                2
            };
            img.set(ox + x, y, 0, col);
        }
    }
}

fn paint_splash(img: &mut Img, ox: i32, big: bool) {
    let drops: [(i32, i32); 10] = [
        (2, 12),
        (5, 8),
        (8, 13),
        (11, 6),
        (14, 11),
        (17, 14),
        (20, 7),
        (23, 12),
        (26, 9),
        (29, 13),
    ];
    for (index, (x, y)) in drops.iter().enumerate() {
        let rise = if big {
            (index as i32 % 4) + 2
        } else {
            index as i32 % 2
        };
        let py = y - rise;
        img.set(ox + x, py, 0, 3);
        img.set(ox + x, py + 1, 0, 2);
        if big && index % 2 == 0 {
            img.set(ox + x + 1, py, 0, 3);
            img.set(ox + x, py - 1, 0, 3);
        }
    }
}

fn arrow_sheet() -> Img {
    let mut img = Img::new(8, 16, true);
    for y in 0..6 {
        let inset = y / 2;
        for x in inset..8 - inset {
            let edge = x == inset || x == 7 - inset || y == 0;
            img.set(x, y, 0, if edge { 3 } else { 2 });
        }
    }
    img.fill(3, 6, 2, 2, 0, 1);
    img
}

/// Paints an 8x8 tile from eight rows of eight characters. `.` keeps the base tile; other
/// characters map through `key` to (palette, colour).
fn ascii_tile(base: Img, rows: [&str; 8], key: &[(char, u8, u8)]) -> Img {
    let mut tile = base;
    for (y, row) in rows.iter().enumerate() {
        assert_eq!(row.len(), 8);
        for (x, ch) in row.chars().enumerate() {
            if ch == '.' {
                continue;
            }
            let (_, pal, col) = key
                .iter()
                .find(|(k, _, _)| *k == ch)
                .unwrap_or_else(|| panic!("scenery key {ch}"));
            tile.set(x as i32, y as i32, *pal, *col);
        }
    }
    tile
}

fn mirror(tile: &Img) -> Img {
    let mut out = Img::new(8, 8, false);
    for y in 0..8 {
        for x in 0..8 {
            out.pixels[y * 8 + x] = tile.pixels[y * 8 + 7 - x];
        }
    }
    out
}

/// The far shore when the coast comes by: a long green land in the island band (map rows 5-8),
/// a hill or two, a small skyline and a tall tiered tower with a needle spire, Taipei 101
/// fashion. The land top sits four rows into map row 7; map row 8 is the beach. Everything
/// is in the island palette (g green, s sand, l light) over the far sea, except the spire
/// which rises into the haze row and is drawn in sky navy.
fn scenery_tiles() -> Vec<Img> {
    let key: &[(char, u8, u8)] = &[('g', ISLAND, 1), ('s', ISLAND, 2), ('l', ISLAND, 3), ('_', ISLAND, 0)];
    let sea = || solid(ISLAND, 0);
    let land_top = ascii_tile(
        sea(),
        [
            "........", "........", "........", "........", "gggggggg", "gggggggg", "gggggggg",
            "gggggggg",
        ],
        key,
    );
    let land = ascii_tile(
        sea(),
        [
            "gggggggg", "g_ggg_gg", "gggggggg", "ggg_gggg", "ssssssss", "ssssssss", "llllllll",
            "........",
        ],
        key,
    );
    let land_top_l = ascii_tile(
        sea(),
        [
            "........", "........", "........", "........", "......gg", "....gggg", "..gggggg",
            ".ggggggg",
        ],
        key,
    );
    let land_l = ascii_tile(
        sea(),
        [
            "gggggggg", ".ggggggg", "..gggggg", "..ssssss", "...sssss", "....ssss", ".....lll",
            "........",
        ],
        key,
    );
    let hill6_l = ascii_tile(
        sea(),
        [
            "........", "........", "........", "......gg", "....gggg", "...ggggg", "..gggggg",
            ".ggggggg",
        ],
        key,
    );
    let hill7_l = ascii_tile(
        sea(),
        [
            ".ggggggg", "gggggggg", "gggggggg", "gggggggg", "gggggggg", "gggggggg", "gggggggg",
            "gggggggg",
        ],
        key,
    );
    // Haze row with the needle: sky palette, drawn over the sky_haze pattern.
    let spire = ascii_tile(
        sky_haze(),
        [
            "........", "...n....", "...n....", "...n....", "...n....", "...n....", "..nnn...",
            "..nnn...",
        ],
        &[('n', SKY, 0)],
    );
    // Tiered body: each tier a lit lip six wide over a four-wide shaft.
    let tower_mid = ascii_tile(
        sea(),
        [
            "...g....", "..ggg...", ".llllll.", "..gggg..", "..gggg..", ".llllll.", "..gggg..",
            "..gggg..",
        ],
        key,
    );
    let tower_base = ascii_tile(
        sea(),
        [
            ".llllll.", "..gggg..", "..gggg..", "llllllll", "gggggggg", "gggggggg", "gggggggg",
            "gggggggg",
        ],
        key,
    );
    let city_a = ascii_tile(
        sea(),
        [
            "..l.....", ".lll.ll.", ".lll.ll.", ".lgl.ll.", "gggggggg", "gggggggg", "gggggggg",
            "gggggggg",
        ],
        key,
    );
    let land_top_r = mirror(&land_top_l);
    let land_r = mirror(&land_l);
    let hill6_r = mirror(&hill6_l);
    let hill7_r = mirror(&hill7_l);
    vec![
        land_top, land, land_top_l, land_top_r, land_l, land_r, hill6_l, hill6_r, hill7_l, hill7_r,
        spire, tower_mid, tower_base, city_a,
    ]
}

fn write_tiles_header(path: &Path, ramp_count: usize, bank1_ramps: usize, scene_base: usize) {
    let text = format!(
        r#"/* Generated by tools/gen_assets. Tile indices in res/bg.png, left to right (VRAM bank 1).
 * Ramp ids below TILE_RAMP1_COUNT are bank-1 tiles TILE_RAMP + id; the rest sit in VRAM bank 0
 * (res/bg0.png) at TILE_RAMP0 + (id - TILE_RAMP1_COUNT), above the sprite patterns. */
#ifndef BEAVER_TILES_H
#define BEAVER_TILES_H

#define TILE_HUD        0
#define TILE_HEART_FULL 1
#define TILE_HEART_HALF 2
#define TILE_HEART_EMPTY 3
#define TILE_DIGIT      4
#define TILE_LETTER     14
#define TILE_METER_DARK 40
#define TILE_METER_GREEN 41
#define TILE_SKY0       42
#define TILE_CLOUD      46
#define TILE_ISLAND     50
#define TILE_SHIP       54
#define TILE_SUN        58
#define TILE_FAR_TOP    62
#define TILE_FAR_BODY   63
#define TILE_FACE       64
#define TILE_BODY       65
#define TILE_ABYSS      66
#define TILE_SPRAY      67
#define TILE_FRONT_DEEP 69
#define TILE_RAMP       70
#define TILE_RAMP_COUNT {ramp_count}
#define TILE_RAMP1_COUNT {bank1_ramps}
#define TILE_RAMP0      {bank0_base}
#define TILE_COUNT      {total}

/* Far-shore scenery, VRAM bank 0 after the ramps (see scenery_tiles in gen_assets). */
#define TILE_SCENE      {scene_base}
#define SC_LAND_TOP     (TILE_SCENE + 0)
#define SC_LAND         (TILE_SCENE + 1)
#define SC_LAND_TOP_L   (TILE_SCENE + 2)
#define SC_LAND_TOP_R   (TILE_SCENE + 3)
#define SC_LAND_L       (TILE_SCENE + 4)
#define SC_LAND_R       (TILE_SCENE + 5)
#define SC_HILL6_L      (TILE_SCENE + 6)
#define SC_HILL6_R      (TILE_SCENE + 7)
#define SC_HILL7_L      (TILE_SCENE + 8)
#define SC_HILL7_R      (TILE_SCENE + 9)
#define SC_SPIRE        (TILE_SCENE + 10)
#define SC_TOWER_MID    (TILE_SCENE + 11)
#define SC_TOWER_BASE   (TILE_SCENE + 12)
#define SC_CITY_A       (TILE_SCENE + 13)
#define SC_CITY_B       SC_CITY_A

/* Background palette index for every tile, matching the indexed PNG palette order. */
#define PAL_HUD 0
#define PAL_SKY 1
#define PAL_CLOUD 2
#define PAL_ISLAND 3
#define PAL_SHIP 4
#define PAL_SUN 5
#define PAL_WATER 6
#define PAL_LINE 7

#endif
"#,
        ramp_count = ramp_count,
        bank1_ramps = bank1_ramps,
        bank0_base = BANK0_RAMP_BASE,
        total = BASE_TILES + bank1_ramps,
        scene_base = scene_base,
    );
    fs::write(path, text).unwrap();
}
