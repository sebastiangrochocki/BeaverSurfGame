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
        [20, 88, 156],
        [56, 168, 64],
        [220, 184, 112],
        [72, 164, 208],
    ], // island / far sea
    [[20, 88, 156], [255, 255, 255], [128, 72, 36], [208, 40, 48]], // ship
    [
        [96, 176, 224],
        [255, 184, 24],
        [255, 228, 128],
        [255, 255, 255],
    ], // sun
    [[4, 20, 56], [12, 64, 128], [48, 156, 212], [240, 252, 255]], // water
    [
        [20, 88, 156],
        [240, 252, 255],
        [48, 156, 212],
        [4, 20, 56],
    ], // waterline: colour 0 is the far-sea blue, so the wave rises out of the sea behind it
];

/// Sprite palettes. Colour 0 is transparent. `sprite_pals` in render.c repeats these in RGB555.
const SP: [[[u8; 3]; 4]; 6] = [
    [[0, 0, 0], [0, 0, 0], [156, 90, 60], [153, 0, 48]],        // beaver: ink, fur, maroon
    [[0, 0, 0], [48, 40, 32], [255, 255, 255], [255, 160, 32]],  // gull
    [[0, 0, 0], [140, 80, 8], [248, 188, 32], [255, 236, 140]], // ring
    [[0, 0, 0], [120, 16, 24], [232, 48, 56], [255, 255, 255]], // heart
    [[0, 0, 0], [16, 80, 150], [180, 220, 240], [255, 255, 255]], // splash
    [[0, 0, 0], [8, 32, 72], [232, 48, 56], [255, 255, 255]],   // arrow
];

const BASE_TILES: usize = 81;
/// Ramps fill VRAM bank 1 above the base tiles first; the rest go to bank 0 above the sprite
/// patterns (render.c checks the sprite sheets really end at BANK0_RAMP_BASE).
const BANK1_RAMP_MAX: usize = 256 - BASE_TILES;
const BANK0_RAMP_BASE: usize = 166;
const BANK0_RAMP_MAX: usize = 256 - BANK0_RAMP_BASE;
/// Eleven pixels per column covers the steepest faces; twelve would cost 24 more tiles for a
/// one-pixel difference and bank 0 is shared with the sprite sheets.
const RAMP_DY_MIN: i32 = -11;
const RAMP_DY_MAX: i32 = 11;
/// The front echo swings a quarter as far as the main wave, so its columns never need the
/// steep ramps; two pixels per column keeps the set small enough for bank 0.
const FRONT_DY_MIN: i32 = -2;
const FRONT_DY_MAX: i32 = 2;
const RAMP_SUB_MIN: i32 = -8;
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

pub fn write(root: &Path) {
    let res = root.join("res");
    let mut ramps = build_ramps();
    let bank1 = ramps.tiles.len().min(BANK1_RAMP_MAX);
    let tiles = tileset(&ramps.tiles[..bank1]);
    tiles.check("bg", 8);
    tiles.write_indexed(&res.join("bg.png"), &BG);
    // Bank 0 holds the main ramps that did not fit bank 1, then every front-wave ramp; the
    // front lookup table is offset so its entries are bank-0 positions directly.
    let spill = ramps.tiles.len() - bank1;
    let scenery = scenery_tiles();
    let scene_base = BANK0_RAMP_BASE + spill + ramps.front_tiles.len();
    let bank0: Vec<&Img> = ramps.tiles[bank1..]
        .iter()
        .chain(ramps.front_tiles.iter())
        .chain(scenery.iter())
        .collect();
    assert!(
        bank0.len() <= BANK0_RAMP_MAX,
        "bank 0 ramps and scenery overflow VRAM ({} > {})",
        bank0.len(),
        BANK0_RAMP_MAX
    );
    for entry in &mut ramps.front_lut {
        *entry = u8::try_from(spill + *entry as usize).expect("front ramp position");
    }
    let tiles0 = tileset0(&bank0);
    tiles0.check("bg0", 8);
    tiles0.write_indexed(&res.join("bg0.png"), &BG);
    write_ramp_header(&root.join("src/ramp.h"), &ramps);

    write_tiles_header(&root.join("src/tiles.h"), ramps.tiles.len(), bank1, scene_base);

    let beaver = beaver_sheet();
    beaver.check("beaver", 1);
    beaver.write_indexed(&res.join("beaver.png"), &SP[0..1]);

    let board = board_sheet();
    board.check("board", 1);
    board.write_indexed(&res.join("board.png"), &SP[0..1]);

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
    sheet.blit_tile(67, &curl_tile(0));
    sheet.blit_tile(68, &curl_tile(1));
    sheet.blit_tile(69, &spray_tile(0));
    sheet.blit_tile(70, &spray_tile(1));
    for sub in 0..8 {
        sheet.blit_tile(71 + sub, &front_line(sub));
    }
    sheet.blit_tile(79, &front_face());
    sheet.blit_tile(80, &front_body_tile());
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
    /// Main-wave ramps; ids index this list.
    tiles: Vec<Img>,
    lut: Vec<u8>,
    /// Front-wave ramps, always placed in bank 0 after the spilled main ramps.
    front_tiles: Vec<Img>,
    front_lut: Vec<u8>,
}

/// Diagonal foam. `sub` is the line's row at the left edge (it may sit outside the tile) and
/// `dy` is how many rows it rises or falls by the right edge. Rows above the line are sky
/// (colour 0), the line is two rows of foam (colour 1) and everything below is lit face
/// (colour 2, the same blue as face_tile); a column the line passes below is all sky and a
/// column it passes above is all face, so a steep face stays a steep face.
fn ramp_from_edges(sub: i32, dy: i32) -> Img {
    let mut tile = solid(LINE, 0);
    for x in 0..8i32 {
        let surface = sub + (dy * x) / 8;
        for y in 0..8i32 {
            let col = if y < surface - 1 {
                0
            } else if y <= surface {
                1
            } else {
                2
            };
            tile.set(x, y, LINE, col);
        }
    }
    tile
}

/// Front-wave crest in the water palette. Above the line the tile carries on the lowest main
/// water row (deep_tile's ripples on the lit base) so the two waves read as one body of water;
/// the line is a white crest with a lit row under it, and below is the shadowed near face that
/// front_body_tile() continues to the bottom of the screen.
fn front_ramp_from_edges(sub: i32, dy: i32) -> Img {
    let mut tile = solid(WATER, 2);
    for x in 0..8i32 {
        let jag = if x % 3 == 0 { -1 } else { 0 };
        let surface = sub + (dy * x) / 8 + jag;
        for y in 0..8i32 {
            let col = if y < surface {
                if (x * 3 + y * 2) % 5 == 0 { 1 } else { 2 }
            } else if y == surface {
                3
            } else if y == surface + 1 {
                2
            } else {
                front_body_colour(x, y)
            };
            tile.set(x, y, WATER, col);
        }
    }
    tile
}

/// Shadowed near-wave face: dark with a few deeper flecks, aligned to the tile grid so a ramp
/// cell and the body cell under it share the pattern.
fn front_body_colour(x: i32, y: i32) -> u8 {
    if (x * 3 + y * 2) % 7 == 0 { 0 } else { 1 }
}

fn front_body_tile() -> Img {
    let mut tile = solid(WATER, 1);
    for y in 0..8i32 {
        for x in 0..8i32 {
            tile.set(x, y, WATER, front_body_colour(x, y));
        }
    }
    tile
}

/// Both ramp sets share one id space: main-wave ramps first, then front-wave ramps. The lookup
/// tables are indexed [dy][sub] so the painter computes one row pointer per column.
fn build_ramps() -> Ramps {
    let sub_count = (RAMP_SUB_MAX - RAMP_SUB_MIN + 1) as usize;
    let mut tiles: Vec<Img> = Vec::new();
    let mut seen: HashMap<Vec<u8>, u8> = HashMap::new();
    let mut intern = |tile: Img| -> u8 {
        if let Some(id) = seen.get(&tile.pixels) {
            *id
        } else {
            let id = tiles.len() as u8;
            seen.insert(tile.pixels.clone(), id);
            tiles.push(tile);
            id
        }
    };
    let mut lut = Vec::new();
    for dy in RAMP_DY_MIN..=RAMP_DY_MAX {
        for sub in RAMP_SUB_MIN..=RAMP_SUB_MAX {
            lut.push(intern(ramp_from_edges(sub, dy)));
        }
    }
    assert!(tiles.len() <= 256, "main ramp ids overflow a byte ({})", tiles.len());
    let mut front_tiles: Vec<Img> = Vec::new();
    let mut front_seen: HashMap<Vec<u8>, u8> = HashMap::new();
    let mut front_lut = Vec::new();
    for dy in FRONT_DY_MIN..=FRONT_DY_MAX {
        for sub in RAMP_SUB_MIN..=RAMP_SUB_MAX {
            let tile = front_ramp_from_edges(sub, dy);
            let id = *front_seen.entry(tile.pixels.clone()).or_insert_with(|| {
                front_tiles.push(tile);
                (front_tiles.len() - 1) as u8
            });
            front_lut.push(id);
        }
    }
    assert_eq!(lut.len() % sub_count, 0);
    Ramps {
        tiles,
        lut,
        front_tiles,
        front_lut,
    }
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
         * RAMP_LUT[(dy - RAMP_DY_MIN) * RAMP_SUB_COUNT + (sub - RAMP_SUB_MIN)] is the main-wave\n\
         * ramp id (see tiles.h for the bank split). FRONT_LUT[(dy - FRONT_DY_MIN) * RAMP_SUB_COUNT\n\
         * + (sub - RAMP_SUB_MIN)] is a VRAM bank 0 tile offset: the front ramp is TILE_RAMP0 +\n\
         * entry. Every entry is a tile; a line that misses the tile maps to an all-water ramp. */\n\
         #ifndef BEAVER_RAMP_H\n\
         #define BEAVER_RAMP_H\n\
         #include <stdint.h>\n\n",
    );
    let sub_count = (RAMP_SUB_MAX - RAMP_SUB_MIN + 1) as usize;
    body.push_str(&format!(
        "#define RAMP_DY_MIN ({})\n#define RAMP_DY_MAX {}\n#define RAMP_SUB_MIN ({})\n#define RAMP_SUB_MAX {}\n#define RAMP_SUB_COUNT {}\n#define FRONT_DY_MIN ({})\n#define FRONT_DY_MAX {}\n\n",
        RAMP_DY_MIN, RAMP_DY_MAX, RAMP_SUB_MIN, RAMP_SUB_MAX, sub_count, FRONT_DY_MIN, FRONT_DY_MAX
    ));
    push_lut(&mut body, "RAMP_LUT", &ramps.lut, sub_count);
    push_lut(&mut body, "FRONT_LUT", &ramps.front_lut, sub_count);
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
    img.fill(0, 0, 16, 16, CLOUD, 0);
    img.oval(5, 9, 4, 3, CLOUD, 2);
    img.oval(10, 10, 4, 2, CLOUD, 2);
    img.oval(8, 8, 6, 3, CLOUD, 1);
    img.oval(5, 8, 3, 2, CLOUD, 1);
    img.oval(11, 9, 3, 2, CLOUD, 1);
    img.set(4, 7, CLOUD, 3);
    img.set(6, 6, CLOUD, 3);
    img.set(9, 6, CLOUD, 3);
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

/// The three water rows share the ramp fill colour, so a ramp cell never shows a hard edge
/// against the water beside it; depth is suggested by the texture alone.
fn water_tile(step: u32, col: u8) -> Img {
    let mut tile = solid(WATER, 2);
    for y in 0..8i32 {
        for x in 0..8i32 {
            if ((x * 3 + y * 2) as u32) % step == 0 {
                tile.set(x, y, WATER, col);
            }
        }
    }
    tile
}

fn face_tile() -> Img {
    // Lit water just under the foam line: a few white sparkles.
    water_tile(13, 3)
}

fn body_tile() -> Img {
    // Mid-depth water: sparse darker ripples.
    water_tile(7, 1)
}

fn deep_tile() -> Img {
    // Lowest water rows: denser ripples leading into the dark foreground wave.
    water_tile(5, 1)
}

fn curl_tile(frame: usize) -> Img {
    let mut tile = solid(LINE, 0);
    for x in 0..8i32 {
        let lip = 2 + ((x as usize + frame * 3) % 4) as i32 / 2;
        if lip > 0 {
            tile.set(x, lip - 1, LINE, 1);
        }
        tile.set(x, lip, LINE, 1);
        tile.set(x, lip + 1, LINE, 2);
        tile.set(x, lip + 2, LINE, 2);
        for y in lip + 3..8 {
            tile.set(x, y, LINE, if (x + y) % 2 == 0 { 3 } else { 2 });
        }
    }
    tile
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

fn front_line(sub: usize) -> Img {
    let mut tile = solid(WATER, 0);
    for x in 0..8i32 {
        let jag = if x % 3 == 0 { -1 } else { 0 };
        let foam = (sub as i32 + jag).clamp(0, 7);
        for y in 0..8i32 {
            let col = if y < foam {
                if y == foam - 1 && x % 2 == 0 { 1 } else { 0 }
            } else if y == foam {
                3
            } else if y == foam + 1 {
                2
            } else {
                1
            };
            tile.set(x, y, WATER, col);
        }
    }
    tile
}

fn front_face() -> Img {
    let mut tile = solid(WATER, 1);
    for x in 0..8i32 {
        tile.set(x, 0, WATER, 3);
        tile.set(x, 1, WATER, 2);
        if x % 2 == 0 {
            tile.set(x, 2, WATER, 2);
        }
        if x % 3 == 1 {
            tile.set(x, 5, WATER, 0);
        }
    }
    tile
}

/// Five 32x32 frames. The resting and crouching beaver are cut above the board (rows 0-23),
/// which is drawn separately so it can tilt and float alone. The flip frames keep the board:
/// the mid-roll is hand drawn, the two quarter turns are the whole resting frame rotated, so
/// the backflip reads as one surfer going round.
fn beaver_sheet() -> Img {
    let mut sheet = Img::new(32 * 5, 32, true);
    let idle = stamp_pose(BEAVER_IDLE);
    blit_frame(&mut sheet, 0, &body_only(&idle));
    blit_frame(&mut sheet, 1, &body_only(&stamp_pose(BEAVER_TUCK)));
    blit_frame(&mut sheet, 2, &rotate_backwards(&idle));
    blit_frame(&mut sheet, 3, &stamp_pose(BEAVER_ROLL));
    blit_frame(&mut sheet, 4, &rotate_forwards(&idle));
    sheet
}

/// Three 32x16 board frames cut from the resting drawing: flat, nose up and nose down. The
/// flat board sits at rows BOARD_LIFT..BOARD_LIFT+8 so the tilted ends have room.
fn board_sheet() -> Img {
    let mut sheet = Img::new(32 * 3, 16, true);
    let idle = stamp_pose(BEAVER_IDLE);
    for x in 0..32 {
        // Nose up lifts the right end two pixels and the middle one; nose down mirrors it.
        let tilt = (x * 3 / 32) as i32;
        for y in BOARD_TOP..32 {
            let pixel = idle.pixels[y * 32 + x];
            if pixel == TRANS {
                continue;
            }
            let row = (y - BOARD_TOP) as i32 + BOARD_LIFT;
            sheet.set(x as i32, row, 0, pixel & 3);
            sheet.set((32 + x) as i32, row - tilt, 0, pixel & 3);
            sheet.set((64 + x) as i32, row + tilt, 0, pixel & 3);
        }
    }
    sheet
}

/// Quarter turn backwards of a square frame: for a right-facing surfer the nose goes up and
/// back.
fn rotate_backwards(src: &Img) -> Img {
    let n = src.width;
    assert_eq!(src.height, n, "rotation needs a square frame");
    let mut dst = Img::new(n, n, true);
    for y in 0..n {
        for x in 0..n {
            dst.pixels[y * n + x] = src.pixels[x * n + (n - 1 - y)];
        }
    }
    dst
}

/// Quarter turn forwards: the last step of the flip, nose coming back down.
fn rotate_forwards(src: &Img) -> Img {
    let n = src.width;
    assert_eq!(src.height, n, "rotation needs a square frame");
    let mut dst = Img::new(n, n, true);
    for y in 0..n {
        for x in 0..n {
            dst.pixels[y * n + x] = src.pixels[(n - 1 - x) * n + y];
        }
    }
    dst
}

/// The board is the bottom eight rows of the surfing drawings.
const BOARD_TOP: usize = 24;
/// Rows of empty space above the flat board in the board sheet (render.c offsets the block).
const BOARD_LIFT: i32 = 2;

fn body_only(src: &Img) -> Img {
    let mut dst = Img::new(src.width, src.height, true);
    let to = BOARD_TOP * src.width;
    dst.pixels[..to].copy_from_slice(&src.pixels[..to]);
    dst
}

fn stamp_pose(art: &str) -> Img {
    let mut img = Img::new(32, 32, true);
    let rows: Vec<&str> = art.lines().filter(|line| !line.is_empty()).collect();
    assert_eq!(rows.len(), 32, "pose height");
    for (y, row) in rows.iter().enumerate() {
        assert_eq!(row.chars().count(), 32, "pose width line {y}");
        for (x, ch) in row.chars().enumerate() {
            let col = match ch {
                'o' => 1,
                'f' => 2,
                'c' => 3,
                '.' => continue,
                other => panic!("bad pixel {other}"),
            };
            img.set(x as i32, y as i32, 0, col);
        }
    }
    img
}

// Hand-drawn frames: o outline, f fur, c maroon (paw, heart, claws, the roll's belly). The
// beaver faces right with the board along the bottom rows.
const BEAVER_IDLE: &str = "
................................
................................
...........ooooo......oooo......
...........offfooooooooffoo.....
...........offoofffffffffffo....
...........offfffffffffffffo....
...........offfffooooffoooofo...
..........offffffoffoffoffofo...
..........offfffffffffffffffo...
.........ooffffffffffffooofffo..
.........offfffffffffffooooffo..
........ooffffffffffffoooooffo..
.oooooooofffffffffffffofofoffoo.
oofffffoffffffffffffffofofofoco.
offfffccoffffoooffffffofofofoco.
offcccccofffoccfofffffofofofoco.
ofccccccofffoccfoffffffooofffoo.
occcccccofffocffofffffffffffffo.
occcccccofffoofoofffffffffffffo.
.occccccoffffooofffffffffffffoo.
.oooccccoffffffffffffffffffffo..
...ooocooffffffffffffffffffooo..
.....ooooffffffffffffffffooccoo.
...oooooooooofffffffffffocffofoo
.oofffooocccooofffffffoooooofffo
oofffffoocffccooffffooooooofffoo
oofffffocfcffffoooooooffffffffoo
offffffocffffffooooofffffffffoo.
oofffffoocfffooffffffffffffooo..
ooofffffoooooffffffffoooooooo...
.oooooooooooooooooooooooooo.....
..ooooooooooooooooooooooo.......
";

/// Crouched low with the paws forward: Down held, or dropping out of a jump.
const BEAVER_TUCK: &str = "
...........oooooo....oooo.......
...........offfoooooooffo.......
...........offfffffffffffo......
...........offffffffffffffo.....
..........oofffffooooffooofo....
..........offffffoffoffofffo....
.........offffffffffffffffffo...
.........offffffffffffffffffo...
........oofffffffffffffoooffo...
...oooo.offffffffffffffooofffo..
..offffoofffffffffffffofofoffo..
..offfffofffffffffffffofofoffo..
.oofffffofffffffffffffofofoffoo.
.offffffoffffoooffffffofofofoco.
.offffccofffofffoffffffoooffoco.
.ofcccccofffoccfoffffffffffffco.
.occccccofffoccfofffffffffffffo.
.occccccoffffoooffffffffffffffo.
.oocccccofffffffffffffffffffffo.
..ooccccoffffffffffffffffffffoo.
...oocccoffffffffffffffffffffooo
....oocooffffffffffffffffffooooo
......oooffffffffffffffffooccooo
....ooooooooofffffffffffocccofoo
...oooooccccoooooooooooooooofffo
.ooffffoccffccooooffffffffffffoo
oofffffocfcfffofffffffffffffffo.
fffffffoooffooofffffffffffffoo..
offffffffoooofffffffffffffooo...
ooffffffffffffffffffffooooo.....
.ooooofffffffffffffoooooo.......
....oooooooooooooooooo..........
";

/// Halfway round the backflip.
const BEAVER_ROLL: &str = "
.......oooooooooooooo...........
......oofffffoooffffoooo........
..oooooffffffoffffffocfo........
.oofoffffoofffffffffooooo.......
.offffffoofffffoofffffffoo......
.ofoffffofffffoooofffffffoo.....
.ofoffffffffffoooooofffooooooo..
..ooffffffffffoooffoffocccccooo.
...offffffffffffoooofoccccccccoo
...offfffffffffffoffocccccccccoo
...offfffffffffffffoccccccccccco
...offffffffffffffocccccccccccco
...offfffffffffffoocccccccccccco
...offfffffffoooooccccccccccccco
...oofffffffoffcocccccccccccccco
....offfffffoccoocccccccccccccco
....ooffffffoccoccccccccccccccco
.....ooffffffoooccccccccccccccco
......offfffffocccccccccccccccoo
......ooffffffocccccccccccccccoo
....oooooooooocccccccccccccccoo.
...oofcccccccocccccccccccccccoo.
.oooffccccccooccccccccccccccco..
ooffffccccccocccccccccccccccoo..
offffffcccccoccccccccccccccoo...
offffffffffcocccccccccccccco....
offffffffffcocccccccccccccoo....
offffffffooooccccccccccccoo.....
oofffffooo..occccccccccooo......
.ooooooo....ooccccccccooo.......
.............ooocccccoo.........
.............ooooooooo..........
";

fn blit_frame(sheet: &mut Img, frame: usize, src: &Img) {
    let ox = frame * src.width;
    for y in 0..src.height {
        for x in 0..src.width {
            sheet.pixels[y * sheet.width + ox + x] = src.pixels[y * src.width + x];
        }
    }
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
    let mut img = Img::new(24, 32, true);
    for y in 0..24i32 {
        for x in 0..24i32 {
            let dx = x - 11;
            let dy = y - 11;
            let d2 = dx * dx + dy * dy;
            if (5 * 5..=11 * 11).contains(&d2) {
                let col = if d2 > 10 * 10 {
                    1
                } else if d2 < 7 * 7 || dy < -3 {
                    3
                } else {
                    2
                };
                img.set(x, y, 0, col);
            }
        }
    }
    img.set(7, 4, 0, 3);
    img.set(8, 4, 0, 3);
    img.set(8, 5, 0, 3);
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
    let mut sheet = Img::new(64, 16, true);
    paint_splash(&mut sheet, 0, false);
    paint_splash(&mut sheet, 32, true);
    sheet
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
    let city_b = ascii_tile(
        sea(),
        [
            "........", "....l...", ".ll.ll..", ".ll.ll.l", "gggggggg", "gggggggg", "gggggggg",
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
        spire, tower_mid, tower_base, city_a, city_b,
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
#define TILE_CURL       67
#define TILE_SPRAY      69
#define TILE_FRONT_LINE 71
#define TILE_FRONT_FACE 79
#define TILE_FRONT_DEEP 80
#define TILE_RAMP       81
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
#define SC_CITY_B       (TILE_SCENE + 14)

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
