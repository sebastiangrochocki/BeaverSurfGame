//! Writes the indexed-color sprites, background tiles, and 10512 Hz sound effects.

use std::fs;
use std::path::Path;

const TRANSPARENT: u8 = 0;
const OUTLINE: u8 = 1;
const FUR_D: u8 = 2;
const FUR: u8 = 3;
const FUR_L: u8 = 4;
const BELLY: u8 = 5;
const WHITE: u8 = 6;
const BOARD: u8 = 7;
const BOARD_D: u8 = 8;
const BOARD_L: u8 = 9;
const GOLD: u8 = 10;
const GOLD_D: u8 = 11;
const GOLD_L: u8 = 12;
const HEART: u8 = 13;
const GRAY: u8 = 14;
const GRAY_D: u8 = 15;
const SKY0: u8 = 16;
const SKY1: u8 = 17;
const SKY2: u8 = 18;
const SKY3: u8 = 19;
const CLOUD_SHADOW: u8 = 20;
const DEEP: u8 = 21;
const DEEP_HI: u8 = 22;
const WATER_MID: u8 = 23;
const WATER_HI: u8 = 24;
const FOAM: u8 = 25;
const FOAM_DIM: u8 = 26;
const GRASS: u8 = 27;
const GRASS_D: u8 = 28;
const GRASS_L: u8 = 29;
const SAND: u8 = 30;
const SAND_D: u8 = 31;
const FAR_HI: u8 = 32;
const FAR_MID: u8 = 33;
const SUN: u8 = 34;
const SUN_L: u8 = 35;
const DEEP_D: u8 = 36;

const PALETTE: [[u8; 4]; 37] = [
    [0, 0, 0, 0],
    [36, 22, 14, 255],
    [92, 48, 26, 255],
    [186, 114, 62, 255],
    [226, 168, 102, 255],
    [246, 216, 176, 255],
    [255, 254, 248, 255],
    [214, 146, 72, 255],
    [112, 64, 32, 255],
    [246, 208, 142, 255],
    [248, 196, 40, 255],
    [176, 112, 16, 255],
    [255, 230, 120, 255],
    [220, 48, 48, 255],
    [150, 172, 190, 255],
    [78, 98, 118, 255],
    [42, 104, 186, 255],
    [86, 162, 226, 255],
    [156, 210, 244, 255],
    [214, 236, 250, 255],
    [148, 184, 208, 255],
    [10, 52, 112, 255],
    [20, 84, 146, 255],
    [28, 118, 186, 255],
    [78, 184, 226, 255],
    [248, 253, 255, 255],
    [196, 228, 242, 255],
    [52, 148, 58, 255],
    [28, 96, 36, 255],
    [138, 206, 78, 255],
    [224, 188, 118, 255],
    [164, 122, 60, 255],
    [126, 202, 232, 255],
    [52, 144, 198, 255],
    [255, 210, 64, 255],
    [255, 242, 176, 255],
    [6, 34, 82, 255],
];

/// Side-view beaver. Tail bands, buck teeth, and the belly patch follow the reference pose.
const BEAVER_ART: &str = "
.................ddd....ddd.....
.................ddffddddffdd...
.................dflllllllfd....
.................dllolllolld....
................dflllllllllfd...
................dllllooooollld..
...............dflllldddddlllfd.
..............dfllllltttttlllfd.
.............dfllllllltttllllfd.
.............dfllllddlldllddlld.
.............dlllllfddllldddlld.
.ddddddddddddflllllllllllllllfd.
dddddddddddddflllllllllllllllfd.
dlfdlfdlfdlfdllllllllllllllllfd.
dddddddddddddlllldddddlldddddo..
.ddddddddddddooooooooooooooooo..
";

struct Image {
    width: usize,
    height: usize,
    pixels: Vec<u8>,
}

impl Image {
    fn new(width: usize, height: usize) -> Self {
        Self {
            width,
            height,
            pixels: vec![TRANSPARENT; width * height],
        }
    }

    fn set(&mut self, x: i32, y: i32, color: u8) {
        if x >= 0 && y >= 0 && (x as usize) < self.width && (y as usize) < self.height {
            self.pixels[y as usize * self.width + x as usize] = color;
        }
    }

    fn fill_rect(&mut self, x: i32, y: i32, w: i32, h: i32, color: u8) {
        for py in y..y + h {
            for px in x..x + w {
                self.set(px, py, color);
            }
        }
    }

    fn fill_oval(&mut self, cx: i32, cy: i32, rx: i32, ry: i32, color: u8) {
        for y in cy - ry..=cy + ry {
            for x in cx - rx..=cx + rx {
                let dx = (x - cx) * ry;
                let dy = (y - cy) * rx;
                if dx * dx + dy * dy <= rx * rx * ry * ry {
                    self.set(x, y, color);
                }
            }
        }
    }

    fn blit_tile(&mut self, index: usize, tile: &Image) {
        let ox = index * 8;
        for y in 0..8 {
            for x in 0..8 {
                self.set(ox as i32 + x, y, tile.pixels[y as usize * 8 + x as usize]);
            }
        }
    }

    fn blit(&mut self, dx: i32, dy: i32, src: &Image) {
        for y in 0..src.height {
            for x in 0..src.width {
                let color = src.pixels[y * src.width + x];
                if color != TRANSPARENT {
                    self.set(dx + x as i32, dy + y as i32, color);
                }
            }
        }
    }

    fn write_png(&self, path: &Path) {
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent).unwrap();
        }
        let mut rgba = Vec::with_capacity(self.pixels.len() * 4);
        for index in &self.pixels {
            rgba.extend_from_slice(&PALETTE[*index as usize]);
        }
        let file = fs::File::create(path).unwrap();
        let mut encoder = png::Encoder::new(file, self.width as u32, self.height as u32);
        encoder.set_color(png::ColorType::Rgba);
        encoder.set_depth(png::BitDepth::Eight);
        encoder
            .write_header()
            .unwrap()
            .write_image_data(&rgba)
            .unwrap();
    }
}

mod gbc;

fn main() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../..")
        .join("gba");
    let gfx = root.join("gfx");
    let sfx = root.join("sfx");

    let tiles = tileset();
    tiles.write_png(&gfx.join("tiles.png"));
    let beaver = beaver_sheet();
    beaver.write_png(&gfx.join("beaver.png"));
    let gulls = gull_sheet();
    gulls.write_png(&gfx.join("gull.png"));
    let hoop = ring();
    hoop.write_png(&gfx.join("ring.png"));
    let spray = splash_sheet();
    spray.write_png(&gfx.join("splash.png"));
    meter_sheet().write_png(&gfx.join("meter.png"));
    pickup_sheet().write_png(&gfx.join("pickup.png"));
    meter_arrow().write_png(&gfx.join("arrow.png"));
    let landing = landing_sheet();
    landing.write_png(&gfx.join("landing.png"));
    scale3(&landing).write_png(Path::new("/tmp/beaver-landing.png"));
    hearts().write_png(&gfx.join("hearts.png"));
    font().write_png(&gfx.join("font.png"));
    write_sfx(&sfx);
    scale3(&preview(&tiles, &beaver, &gulls, &hoop, &spray))
        .write_png(Path::new("/tmp/beaver-preview.png"));

    let gbc_root = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../..")
        .join("gbc");
    gbc::write(&gbc_root);
}

fn tile() -> Image {
    Image::new(8, 8)
}

fn solid(color: u8) -> Image {
    let mut tile = tile();
    tile.fill_rect(0, 0, 8, 8, color);
    tile
}

/// 0 empty, 1-4 sky, 5-8 cloud, 9 deep, 10 far top, 11 far body,
/// 12-27 waterline, 28-31 splash, 32-39 island, 40-47 ship, 48-51 sun,
/// 52 front deep, 53-60 front waterline, 61-70 wave faces, 71-85 shore.
fn tileset() -> Image {
    let mut sheet = Image::new(8 * 86, 8);
    sheet.blit_tile(0, &tile());
    sheet.blit_tile(1, &sky_band(SKY0, SKY1));
    sheet.blit_tile(2, &sky_band(SKY1, SKY2));
    sheet.blit_tile(3, &sky_band(SKY2, SKY3));
    sheet.blit_tile(4, &solid(SKY3));
    blit_image_tiles(&mut sheet, 5, &cloud_art());
    sheet.blit_tile(9, &deep_tile());
    sheet.blit_tile(10, &far_top());
    sheet.blit_tile(11, &far_body());
    for variant in 0..2 {
        for sub in 0..8 {
            sheet.blit_tile(12 + variant * 8 + sub, &waterline(sub, variant));
        }
    }
    for kind in 0..4 {
        sheet.blit_tile(28 + kind, &splash_tile(kind));
    }
    let mut next = blit_image_tiles(&mut sheet, 32, &island_art());
    next = blit_image_tiles(&mut sheet, next, &ship_art());
    next = blit_image_tiles(&mut sheet, next, &sun_art());
    sheet.blit_tile(next, &front_deep_tile());
    for sub in 0..8 {
        sheet.blit_tile(next + 1 + sub, &front_waterline(sub));
    }
    // 61..64 face, 65..66 body, 67 abyss, 68..69 curl, 70 front face.
    for variant in 0..4 {
        sheet.blit_tile(61 + variant, &face_tile(variant));
    }
    for variant in 0..2 {
        sheet.blit_tile(65 + variant, &body_tile(variant));
    }
    sheet.blit_tile(67, &abyss_tile());
    for frame in 0..2 {
        sheet.blit_tile(68 + frame, &curl_tile(frame));
    }
    sheet.blit_tile(70, &front_face_tile());
    for (i, tile) in shore_tiles().iter().enumerate() {
        sheet.blit_tile(71 + i, tile);
    }
    sheet
}

/// Paints over `base` from eight rows of eight characters; `.` keeps the base pixel.
fn ascii_tile(base: Image, rows: [&str; 8], key: &[(char, u8)]) -> Image {
    let mut tile = base;
    for (y, row) in rows.iter().enumerate() {
        assert_eq!(row.len(), 8);
        for (x, ch) in row.chars().enumerate() {
            if ch == '.' {
                continue;
            }
            let (_, color) = key
                .iter()
                .find(|(k, _)| *k == ch)
                .unwrap_or_else(|| panic!("shore key {ch}"));
            tile.set(x as i32, y as i32, *color);
        }
    }
    tile
}

fn mirror(tile: &Image) -> Image {
    let mut out = Image::new(8, 8);
    for y in 0..8 {
        for x in 0..8 {
            out.pixels[y * 8 + x] = tile.pixels[y * 8 + 7 - x];
        }
    }
    out
}

/// The far shore, same drawings as the Game Boy Color port (gbc.rs scenery_tiles): a long
/// green country with hills, a skyline and a tiered tower with a needle spire. Rows of the
/// island layer, top to bottom: spire (over sky), tower body or hill top (over sky), land
/// top (over the horizon row), beach (over the far sea).
/// Order: land top, land, land top L/R, land L/R, hill upper L/R, hill lower L/R, spire,
/// tower mid, tower base, city A, city B.
fn shore_tiles() -> Vec<Image> {
    let key: &[(char, u8)] = &[
        ('g', GRASS),
        ('_', GRASS_D),
        ('s', SAND),
        ('l', GRASS_L),
        ('f', FOAM_DIM),
        ('c', GRAY),
        ('n', DEEP_D),
    ];
    let land_top = ascii_tile(
        tile(),
        [
            "........", "........", "........", "........", "gggggggg", "gggggggg", "gggggggg",
            "gggggggg",
        ],
        key,
    );
    let land = ascii_tile(
        far_top(),
        [
            "gggggggg", "g_ggg_gg", "gggggggg", "ggg_gggg", "ssssssss", "ssssssss", "ffffffff",
            "........",
        ],
        key,
    );
    let land_top_l = ascii_tile(
        tile(),
        [
            "........", "........", "........", "........", "......gg", "....gggg", "..gggggg",
            ".ggggggg",
        ],
        key,
    );
    let land_l = ascii_tile(
        far_top(),
        [
            "gggggggg", ".ggggggg", "..gggggg", "..ssssss", "...sssss", "....ssss", ".....fff",
            "........",
        ],
        key,
    );
    let hill6_l = ascii_tile(
        tile(),
        [
            "........", "........", "........", "......gg", "....gggg", "...ggggg", "..gggggg",
            ".ggggggg",
        ],
        key,
    );
    let hill7_l = ascii_tile(
        tile(),
        [
            ".ggggggg", "gggggggg", "gggggggg", "gggggggg", "gggggggg", "gggggggg", "gggggggg",
            "gggggggg",
        ],
        key,
    );
    let spire = ascii_tile(
        tile(),
        [
            "........", "...n....", "...n....", "...n....", "...n....", "...n....", "..nnn...",
            "..nnn...",
        ],
        key,
    );
    let tower_mid = ascii_tile(
        tile(),
        [
            "...g....", "..ggg...", ".llllll.", "..gggg..", "..gggg..", ".llllll.", "..gggg..",
            "..gggg..",
        ],
        key,
    );
    let tower_base = ascii_tile(
        tile(),
        [
            ".llllll.", "..gggg..", "..gggg..", "llllllll", "gggggggg", "gggggggg", "gggggggg",
            "gggggggg",
        ],
        key,
    );
    let city_a = ascii_tile(
        tile(),
        [
            "..c.....", ".ccc.cc.", ".ccc.cc.", ".cgc.cc.", "gggggggg", "gggggggg", "gggggggg",
            "gggggggg",
        ],
        key,
    );
    let city_b = ascii_tile(
        tile(),
        [
            "........", "....c...", ".cc.cc..", ".cc.cc.c", "gggggggg", "gggggggg", "gggggggg",
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

/// First row under the waterline: lit water fading down, with diagonal streaks that
/// animate across the four variants.
fn face_tile(variant: usize) -> Image {
    let mut tile = tile();
    for y in 0..8i32 {
        for x in 0..8i32 {
            let streak = (x + y + variant as i32 * 2) % 8 == 0 && y < 6;
            let color = if streak {
                WATER_HI
            } else if y < 2 {
                if (x + y) % 2 == 0 {
                    WATER_HI
                } else {
                    WATER_MID
                }
            } else if y < 4 {
                WATER_MID
            } else if y < 6 {
                if (x + y) % 2 == 0 { WATER_MID } else { DEEP_HI }
            } else if (x + y) % 2 == 0 {
                DEEP_HI
            } else {
                DEEP
            };
            tile.set(x, y, color);
        }
    }
    tile
}

/// Second row under the waterline: dark water with a drifting highlight streak and a bubble.
fn body_tile(variant: usize) -> Image {
    let mut tile = tile();
    for y in 0..8i32 {
        for x in 0..8i32 {
            let streak_y = 2 + variant as i32 * 3;
            let streak = y == streak_y && (1..6).contains(&x);
            let bubble = y == 6 - variant as i32 * 2 && x == 6;
            let color = if streak {
                DEEP_HI
            } else if bubble {
                FOAM_DIM
            } else if (x + y) % 2 == 0 {
                DEEP_HI
            } else {
                DEEP
            };
            tile.set(x, y, color);
        }
    }
    tile
}

/// Deep rows: darker water so the wave reads as a thick body of water.
fn abyss_tile() -> Image {
    let mut tile = tile();
    for y in 0..8i32 {
        for x in 0..8i32 {
            let color = if (x + y) % 2 == 0 { DEEP } else { DEEP_D };
            tile.set(x, y, color);
        }
    }
    tile
}

/// Lip of a breaking wave, drawn above the waterline on steep columns.
fn curl_tile(frame: usize) -> Image {
    let mut tile = tile();
    let lift = frame as i32;
    for x in 0..8i32 {
        let top = (3 + lift - (x - 3).abs() / 3).max(1 + lift);
        for y in top..8 {
            let color = if y == top {
                if (x + frame as i32) % 2 == 0 {
                    WHITE
                } else {
                    FOAM
                }
            } else if y == top + 1 {
                FOAM
            } else if y < 6 {
                if (x + y) % 2 == 0 { FOAM } else { FOAM_DIM }
            } else {
                FOAM_DIM
            };
            tile.set(x, y, color);
        }
        if x % 3 == frame as i32 {
            tile.set(x, top - 1, WHITE);
        }
    }
    for x in [1, 4, 6] {
        tile.set(x, 7, WATER_HI);
    }
    tile
}

fn front_face_tile() -> Image {
    let mut tile = tile();
    for y in 0..8i32 {
        for x in 0..8i32 {
            let color = if y == 0 {
                if x % 3 == 0 { FOAM_DIM } else { DEEP_HI }
            } else if y == 3 && (2..6).contains(&x) {
                DEEP_HI
            } else if (x + y * 2) % 3 == 0 {
                OUTLINE
            } else {
                DEEP
            };
            tile.set(x, y, color);
        }
    }
    tile
}

fn blit_image_tiles(sheet: &mut Image, start: usize, image: &Image) -> usize {
    let cols = image.width / 8;
    let rows = image.height / 8;
    let mut index = start;
    for row in 0..rows {
        for col in 0..cols {
            let mut part = tile();
            for y in 0..8 {
                for x in 0..8 {
                    let color = image.pixels[(row * 8 + y) * image.width + col * 8 + x];
                    part.set(x as i32, y as i32, color);
                }
            }
            sheet.blit_tile(index, &part);
            index += 1;
        }
    }
    index
}

fn sky_band(main: u8, _next: u8) -> Image {
    solid(main)
}

fn cloud_art() -> Image {
    let mut img = Image::new(16, 16);
    img.fill_oval(8, 10, 7, 4, CLOUD_SHADOW);
    img.fill_oval(5, 11, 4, 3, CLOUD_SHADOW);
    img.fill_oval(11, 11, 4, 3, CLOUD_SHADOW);
    img.fill_oval(8, 8, 7, 4, WHITE);
    img.fill_oval(5, 9, 4, 3, WHITE);
    img.fill_oval(11, 9, 4, 3, WHITE);
    img.fill_oval(8, 6, 3, 3, WHITE);
    img
}

fn deep_tile() -> Image {
    let mut tile = tile();
    for y in 0..8 {
        for x in 0..8 {
            let color = if (x + y) % 2 == 0 { DEEP } else { DEEP_HI };
            tile.set(x, y, color);
        }
    }
    tile
}

fn front_deep_tile() -> Image {
    let mut tile = tile();
    for y in 0..8 {
        for x in 0..8 {
            let color = if (x + y * 2) % 3 == 0 { OUTLINE } else { DEEP };
            tile.set(x, y, color);
        }
    }
    tile
}

fn front_waterline(sub: usize) -> Image {
    let mut tile = tile();
    for x in 0..8i32 {
        let jag = match x % 4 {
            0 => -1,
            2 => 1,
            _ => 0,
        };
        let foam = (sub as i32 + jag).clamp(0, 7);
        for y in 0..8i32 {
            let color = if y == foam - 1 && x % 2 == 0 {
                WHITE
            } else if y == foam {
                FOAM
            } else if y == foam + 1 {
                DEEP_HI
            } else if y > foam + 1 {
                if (x + y * 2) % 3 == 0 { OUTLINE } else { DEEP }
            } else {
                TRANSPARENT
            };
            tile.set(x, y, color);
        }
    }
    tile
}

fn far_top() -> Image {
    let mut tile = tile();
    for y in 0..8 {
        for x in 0..8 {
            let color = if y == 0 {
                if x % 3 == 0 { WHITE } else { FOAM }
            } else if y == 1 {
                if x % 2 == 0 { FAR_HI } else { FOAM_DIM }
            } else if (x + y) % 2 == 0 {
                FAR_HI
            } else {
                FAR_MID
            };
            tile.set(x, y, color);
        }
    }
    tile
}

fn far_body() -> Image {
    let mut tile = tile();
    for y in 0..8 {
        for x in 0..8 {
            let sparkle = y == 3 && x % 7 == 2;
            let color = if sparkle {
                WHITE
            } else if (x + y) % 2 == 0 {
                FAR_HI
            } else {
                FAR_MID
            };
            tile.set(x, y, color);
        }
    }
    tile
}

fn waterline(sub: usize, variant: usize) -> Image {
    let mut tile = tile();
    for x in 0..8i32 {
        let jag = match (x as usize + variant * 2) % 4 {
            0 => -1,
            2 => 1,
            _ => 0,
        };
        let foam = (sub as i32 + jag).clamp(0, 7);
        for y in 0..8i32 {
            let color = if y == foam - 3 && (x as usize + variant) % 5 == 0 {
                WHITE
            } else if y == foam - 2 && (x as usize + variant) % 3 == 0 {
                WATER_HI
            } else if y == foam - 1 && (x as usize + variant) % 3 != 1 {
                if x % 2 == 0 { WHITE } else { FOAM }
            } else if y == foam {
                if (x as usize + variant) % 5 == 0 {
                    WHITE
                } else {
                    FOAM
                }
            } else if y == foam + 1 {
                if x % 2 == 0 { WATER_HI } else { FOAM_DIM }
            } else if y > foam + 1 {
                if (x + y) % 2 == 0 { WATER_MID } else { DEEP }
            } else {
                TRANSPARENT
            };
            tile.set(x, y, color);
        }
    }
    tile
}

fn splash_tile(kind: usize) -> Image {
    let mut tile = tile();
    let spec: &[(i32, i32, u8)] = match kind {
        0 => &[
            (1, 6, FOAM),
            (2, 7, WHITE),
            (3, 5, WATER_HI),
            (6, 7, FOAM),
            (5, 6, FOAM_DIM),
        ],
        1 => &[
            (0, 7, WHITE),
            (4, 6, FOAM),
            (5, 7, FOAM),
            (7, 5, WATER_HI),
            (2, 7, FOAM_DIM),
        ],
        2 => &[
            (1, 4, WATER_HI),
            (2, 6, FOAM),
            (3, 7, WHITE),
            (4, 5, FOAM),
            (6, 7, FOAM),
            (5, 3, WHITE),
        ],
        _ => &[
            (0, 5, FOAM),
            (2, 7, WHITE),
            (3, 6, FOAM),
            (6, 4, WATER_HI),
            (7, 7, FOAM),
            (4, 7, WHITE),
        ],
    };
    for (x, y, color) in spec {
        tile.set(*x, *y, *color);
    }
    tile
}

fn island_art() -> Image {
    let mut img = Image::new(32, 16);
    img.fill_oval(16, 14, 14, 6, SAND_D);
    img.fill_oval(16, 12, 13, 4, SAND);
    img.fill_oval(15, 9, 12, 4, GRASS_D);
    img.fill_oval(14, 8, 11, 3, GRASS);
    for x in (4..28).step_by(3) {
        img.set(x, 5, GRASS_L);
        img.set(x, 6, GRASS);
        img.set(x + 1, 6, GRASS_L);
    }
    for y in 3..11 {
        img.set(22, y, BOARD_D);
        img.set(23, y, BOARD);
    }
    for (dx, dy) in [
        (-3, 1),
        (-2, 0),
        (-1, -1),
        (0, -1),
        (1, -1),
        (2, 0),
        (3, 1),
        (-2, 2),
        (2, 2),
    ] {
        img.set(22 + dx, 3 + dy, GRASS_D);
        img.set(23 + dx, 3 + dy, GRASS_L);
    }
    img.set(6, 13, GRAY_D);
    img.set(10, 14, GRAY);
    img.set(18, 14, SAND_D);
    img.set(4, 13, FOAM);
    img.set(5, 14, FOAM);
    img.set(27, 13, FOAM);
    img.set(28, 14, WHITE);
    img
}

fn ship_art() -> Image {
    let mut img = Image::new(32, 16);
    for x in 2..30 {
        let taper = if x < 7 {
            (7 - x) / 2
        } else if x > 25 {
            (x - 25) / 2
        } else {
            0
        };
        for y in (9 + taper)..14 {
            let color = if y >= 13 {
                BOARD_D
            } else if y == 9 + taper {
                BOARD_L
            } else if x % 6 == 0 {
                BOARD_D
            } else {
                BOARD
            };
            img.set(x, y, color);
        }
        img.set(x, 14, OUTLINE);
    }
    img.fill_rect(16, 6, 7, 4, WHITE);
    img.fill_rect(16, 6, 7, 1, GRAY);
    img.fill_rect(22, 6, 1, 4, GRAY_D);
    img.set(18, 8, WATER_HI);
    img.set(19, 8, OUTLINE);
    img.fill_rect(12, 1, 1, 9, OUTLINE);
    img.fill_rect(13, 2, 5, 5, WHITE);
    img.fill_rect(17, 2, 1, 5, GRAY);
    img.set(14, 3, GRAY);
    img.fill_rect(9, 1, 3, 2, HEART);
    img.set(28, 11, FOAM);
    img.set(29, 12, WHITE);
    img.set(30, 13, FOAM_DIM);
    img.set(3, 12, FOAM);
    img.set(2, 13, FOAM_DIM);
    img
}

fn sun_art() -> Image {
    let mut img = Image::new(16, 16);
    img.fill_oval(8, 8, 6, 6, SUN);
    img.fill_oval(7, 7, 3, 3, SUN_L);
    img.set(4, 4, WHITE);
    img
}

/// Frames 0-3 are flat, climbing, descending, and airborne. Frames 4-6 are the airborne pose
/// turned 90, 180, and 270 degrees backwards for the backflip. Frame 7 is the empty board.
fn beaver_sheet() -> Image {
    let mut sheet = Image::new(256, 32);
    paint_board(&mut sheet, 224, 0);
    let poses = [(0, 0), (-1, -1), (1, 1), (-3, -2)];
    for (frame, (dy, tilt)) in poses.iter().enumerate() {
        let ox = frame as i32 * 32;
        paint_board(&mut sheet, ox, *tilt);
        stamp_sheared(&mut sheet, ox, 9 + dy, BEAVER_ART, 32, *tilt);
    }
    let mut turned = Image::new(32, 32);
    for y in 0..32 {
        for x in 0..32 {
            turned.set(
                x,
                y,
                sheet.pixels[y as usize * sheet.width + 96 + x as usize],
            );
        }
    }
    for frame in 4..7 {
        turned = rotate_backwards(&turned);
        for y in 0..32 {
            for x in 0..32 {
                sheet.set(
                    frame * 32 + x,
                    y,
                    turned.pixels[y as usize * 32 + x as usize],
                );
            }
        }
    }
    sheet
}

/// Quarter turn counter-clockwise: for a right-facing surfer, the nose goes up and back.
fn rotate_backwards(src: &Image) -> Image {
    let mut dst = Image::new(32, 32);
    for y in 0..32i32 {
        for x in 0..32i32 {
            dst.set(y, 31 - x, src.pixels[y as usize * 32 + x as usize]);
        }
    }
    dst
}

fn paint_board(img: &mut Image, ox: i32, tilt: i32) {
    let slope = match tilt {
        -2 => -3,
        -1 => -2,
        1 => 2,
        _ => 0,
    };
    for x in 1..=30 {
        let dy = ((x - 15) * slope) / 7;
        let rocker = if x >= 27 {
            -2
        } else if x >= 23 {
            -1
        } else if x <= 3 {
            -1
        } else {
            0
        };
        let cy = 26 + dy + rocker;
        let half = if x <= 2 || x >= 29 {
            1
        } else if x <= 5 || x >= 26 {
            2
        } else {
            3
        };
        img.set(ox + x, cy - half, OUTLINE);
        for d in (-half + 1)..half {
            let color = if d == -half + 1 {
                if (7..=24).contains(&x) && x % 3 == 0 {
                    WHITE
                } else {
                    BOARD_L
                }
            } else if d == 0 {
                BOARD_D
            } else if d == half - 1 {
                BOARD_D
            } else {
                BOARD
            };
            img.set(ox + x, cy + d, color);
        }
        img.set(ox + x, cy + half, OUTLINE);
        img.set(ox + x, cy + half + 1, OUTLINE);
    }
    let fin_y = 26 + ((-11) * slope) / 7 + 3;
    img.set(ox + 4, fin_y + 1, OUTLINE);
    img.set(ox + 5, fin_y + 1, BOARD_D);
    img.set(ox + 5, fin_y + 2, OUTLINE);
    img.set(ox + 6, fin_y + 1, OUTLINE);
}

fn gull_sheet() -> Image {
    let mut sheet = Image::new(64, 16);
    paint_gull(&mut sheet, 0, true);
    paint_gull(&mut sheet, 32, false);
    sheet
}

fn paint_gull(img: &mut Image, ox: i32, wings_up: bool) {
    let mut right = Image::new(32, 16);
    right.fill_oval(14, 10, 5, 2, GRAY_D);
    right.fill_oval(14, 9, 5, 2, WHITE);
    right.fill_oval(21, 8, 3, 2, OUTLINE);
    right.fill_oval(21, 8, 2, 1, WHITE);
    right.set(22, 8, OUTLINE);
    right.set(22, 7, WHITE);
    right.fill_rect(24, 8, 5, 1, GOLD);
    right.set(28, 8, GOLD_L);
    right.set(28, 9, GOLD_D);
    right.fill_rect(7, 9, 4, 2, GRAY);
    right.set(6, 8, GRAY_D);
    right.set(6, 10, WHITE);
    if wings_up {
        right.fill_rect(8, 3, 12, 2, GRAY_D);
        right.fill_rect(9, 2, 10, 2, WHITE);
        right.fill_rect(11, 1, 6, 2, WHITE);
        right.fill_rect(10, 5, 8, 1, GRAY);
        right.set(12, 4, GRAY_D);
        right.set(18, 4, GRAY_D);
    } else {
        right.fill_rect(6, 12, 14, 2, GRAY_D);
        right.fill_rect(7, 11, 12, 2, WHITE);
        right.fill_rect(9, 13, 8, 2, WHITE);
        right.fill_rect(10, 10, 6, 1, GRAY);
        right.set(8, 14, GRAY_D);
        right.set(20, 14, GRAY_D);
    }
    // Mirror horizontally so the gull faces left (its flight direction).
    for y in 0..16 {
        for x in 0..32 {
            let c = right.pixels[y * 32 + x];
            img.set(ox + 31 - x as i32, y as i32, c);
        }
    }
}

fn ring() -> Image {
    let mut img = Image::new(32, 32);
    for y in 0..32 {
        for x in 0..32 {
            let dx = x * 2 - 31;
            let dy = y * 2 - 31;
            let dist = dx * dx + dy * dy;
            if dist < 15 * 15 || dist > 29 * 29 {
                continue;
            }
            let color = if dist >= 27 * 27 {
                OUTLINE
            } else if dist <= 18 * 18 {
                if dy < 0 { GOLD_L } else { GOLD_D }
            } else if dy < -6 {
                GOLD_L
            } else if dy > 10 {
                GOLD_D
            } else {
                GOLD
            };
            img.set(x, y, color);
        }
    }
    img.set(11, 6, WHITE);
    img.set(12, 6, WHITE);
    img.set(12, 7, GOLD_L);
    img.set(13, 8, GOLD_L);
    img
}

/// Jump-timing bar in three 32x8 pieces: early, green window, too late.
fn meter_sheet() -> Image {
    let mut sheet = Image::new(96, 8);
    for x in 0..96 {
        if (32..64).contains(&x) {
            sheet.fill_rect(x, 1, 1, 6, GRASS_L);
            sheet.set(x, 7, GRASS);
        } else {
            sheet.fill_rect(x, 3, 1, 3, DEEP);
        }
    }
    sheet
}

fn meter_arrow() -> Image {
    let mut img = Image::new(8, 8);
    for row in 0..7 {
        let inset = (row + 1) / 2;
        for x in inset..8 - inset {
            let edge = x == inset || x == 7 - inset || row == 0;
            img.set(x, row, if edge { OUTLINE } else { HEART });
        }
    }
    img
}

/// Three frames of the big splash thrown up by a clean downhill landing.
fn landing_sheet() -> Image {
    let mut sheet = Image::new(96, 32);
    for stage in 0..3 {
        paint_landing(&mut sheet, stage * 32, stage);
    }
    sheet
}

fn paint_landing(img: &mut Image, ox: i32, stage: i32) {
    let mut dot = |x: i32, y: i32, color: u8| {
        if (0..32).contains(&x) && (0..32).contains(&y) {
            img.set(ox + x, y, color);
        }
    };
    let t = (stage + 1) as f32 / 3.0;

    let wall: i32 = [16, 20, 0][stage as usize];
    for side in [-1, 1] {
        for h in 0..wall {
            if h > wall * 3 / 4 && h % 2 == 0 {
                continue;
            }
            let x = 16 + side * (2 + h * 2 / 3 + stage * 2);
            let y = 30 - h;
            dot(x, y, WHITE);
            dot(x - side, y, if h % 3 == 0 { FOAM } else { WATER_HI });
            dot(x - 2 * side, y, WATER_HI);
        }
    }
    let spout: i32 = [9, 5, 0][stage as usize];
    for h in 0..spout {
        for dx in -2i32..=2 {
            let color = if dx.abs() == 2 || h == spout - 1 {
                FOAM
            } else {
                WATER_HI
            };
            dot(16 + dx, 30 - h, color);
        }
    }

    let crown = 5 + stage * 4;
    for dx in -crown..=crown {
        let lift = ((crown - dx.abs()) as f32 * 0.35 * (1.0 - t * 0.6)) as i32;
        dot(
            16 + dx,
            30 - lift,
            if dx.rem_euclid(3) == 0 { WHITE } else { FOAM },
        );
        dot(16 + dx, 31, FOAM_DIM);
    }

    for k in 0..20 {
        let a = k as f32 / 19.0 * 2.0 - 1.0;
        let vx = a * 17.0;
        let vy = 22.0 + a.abs() * 10.0 + (k % 3) as f32 * 4.0;
        let x = 16 + (vx * t) as i32;
        let y = 30 - (vy * t - 26.0 * t * t) as i32;
        let color = match k % 3 {
            0 => WHITE,
            1 => FOAM,
            _ => WATER_HI,
        };
        dot(x, y, color);
        if stage < 2 {
            dot(x + 1, y, color);
            dot(x, y + 1, FOAM_DIM);
        }
    }
}

/// Half-heart pickup, 16x16, two frames: resting and glowing.
fn pickup_sheet() -> Image {
    let mut sheet = Image::new(32, 16);
    for frame in 0..2 {
        let ox = frame * 16;
        let lift = frame;
        for (x, y) in [(4, 3), (11, 3)] {
            sheet.fill_oval(ox + x, y + 1 - lift, 3, 3, HEART);
        }
        for row in 0..7 {
            let half = 6 - row;
            sheet.fill_rect(ox + 8 - half, 5 + row - lift, half * 2 - 1, 1, HEART);
        }
        for x in 2..14 {
            let y = 4 - lift;
            if sheet.pixels[(y as usize) * 32 + (ox + x) as usize] == HEART {
                sheet.set(ox + x, y, GOLD_L);
            }
        }
        sheet.set(ox + 5, 3 - lift, WHITE);
        sheet.set(ox + 6, 4 - lift, WHITE);
        if frame == 1 {
            for (x, y) in [(1, 1), (14, 2), (2, 12), (13, 12), (8, 0)] {
                sheet.set(ox + x, y, GOLD_L);
            }
        }
    }
    sheet
}

fn splash_sheet() -> Image {
    let mut sheet = Image::new(64, 16);
    paint_wake(&mut sheet, 0, false);
    paint_wake(&mut sheet, 32, true);
    sheet
}

fn paint_wake(img: &mut Image, ox: i32, burst: bool) {
    let drops: &[(i32, i32)] = if burst {
        &[
            (2, 3),
            (3, 6),
            (4, 9),
            (5, 12),
            (7, 4),
            (8, 8),
            (9, 13),
            (22, 3),
            (23, 7),
            (24, 11),
            (26, 5),
            (27, 9),
            (28, 13),
            (1, 14),
            (6, 15),
            (14, 14),
            (18, 15),
            (25, 14),
            (30, 13),
        ]
    } else {
        &[
            (2, 12),
            (4, 14),
            (6, 11),
            (8, 15),
            (11, 14),
            (21, 12),
            (23, 15),
            (25, 11),
            (27, 14),
            (30, 13),
            (14, 15),
            (18, 14),
        ]
    };
    for (i, (x, y)) in drops.iter().enumerate() {
        let color = match i % 3 {
            0 => WHITE,
            1 => FOAM,
            _ => WATER_HI,
        };
        img.set(ox + x, *y, color);
        if burst && i % 2 == 0 {
            img.set(ox + x, y + 1, FOAM_DIM);
        }
    }
}

fn hearts() -> Image {
    let mut sheet = Image::new(24, 8);
    stamp_heart(&mut sheet, 0, true, true);
    stamp_heart(&mut sheet, 8, true, false);
    stamp_heart(&mut sheet, 16, false, false);
    sheet
}

fn stamp_heart(img: &mut Image, ox: i32, left: bool, right: bool) {
    const ROWS: [u8; 8] = [
        0b0110_0110,
        0b1111_1111,
        0b1111_1111,
        0b1111_1111,
        0b0111_1110,
        0b0011_1100,
        0b0001_1000,
        0b0000_0000,
    ];
    for (y, row) in ROWS.iter().enumerate() {
        for x in 0..8 {
            if row & (0x80 >> x) == 0 {
                continue;
            }
            let filled = (x < 4 && left) || (x >= 4 && right);
            img.set(ox + x, y as i32, if filled { HEART } else { OUTLINE });
        }
    }
    if left {
        img.set(ox + 1, 1, WHITE);
    }
    if right {
        img.set(ox + 4, 1, WHITE);
    }
}

fn stamp_sheared(img: &mut Image, ox: i32, oy: i32, art: &str, width: usize, tilt: i32) {
    let rows: Vec<&str> = art.lines().filter(|line| !line.is_empty()).collect();
    let mut bad = Vec::new();
    for (y, row) in rows.iter().enumerate() {
        let count = row.chars().count();
        if count != width {
            bad.push(format!("row {y} has {count}"));
        }
    }
    if !bad.is_empty() {
        panic!("art width {width}:\n{}", bad.join("\n"));
    }
    let slope = match tilt {
        -2 => -3,
        -1 => -2,
        1 => 2,
        _ => 0,
    };
    for (y, row) in rows.iter().enumerate() {
        for (x, ch) in row.chars().enumerate() {
            if let Some(color) = paint_char(ch) {
                let shear = ((x as i32 - 18) * slope) / 9;
                img.set(ox + x as i32, oy + y as i32 + shear, color);
            }
        }
    }
}

fn paint_char(ch: char) -> Option<u8> {
    Some(match ch {
        '.' => return None,
        'o' => OUTLINE,
        'd' => FUR_D,
        'f' => FUR,
        'l' => FUR_L,
        'b' => BELLY,
        'w' => WHITE,
        'n' => OUTLINE,
        't' => WHITE,
        'B' => BOARD,
        'D' => BOARD_D,
        'L' => BOARD_L,
        'k' => GOLD,
        'a' => GRAY,
        'F' => FOAM,
        'h' => WATER_HI,
        _ => panic!("unknown art char {ch}"),
    })
}

fn preview(tiles: &Image, beaver: &Image, gulls: &Image, hoop: &Image, spray: &Image) -> Image {
    let mut img = Image::new(240, 160);
    for row in 0..20 {
        let band = match row {
            0..=2 => 1,
            3..=5 => 2,
            6..=8 => 3,
            _ => 4,
        };
        for col in 0..30 {
            blit_sheet_tile(&mut img, tiles, band, col * 8, row * 8);
        }
    }
    blit_block(&mut img, tiles, 48, 2, 2, 200, 8);
    blit_block(&mut img, tiles, 5, 2, 2, 16, 16);
    blit_block(&mut img, tiles, 5, 2, 2, 120, 28);
    blit_block(&mut img, tiles, 5, 2, 2, 180, 12);
    for col in 0..30 {
        blit_sheet_tile(&mut img, tiles, 10, col * 8, 11 * 8);
        for row in 12..20 {
            blit_sheet_tile(&mut img, tiles, 11, col * 8, row * 8);
        }
    }
    blit_block(&mut img, tiles, 32, 4, 2, 8, 72);
    blit_block(&mut img, tiles, 40, 4, 2, 150, 72);
    for col in 0..30 {
        let wave = (col as f32 * 0.55).sin() * 12.0 + (col as f32 * 0.22).cos() * 5.0;
        let surface = 108 + wave as i32;
        let row = surface / 8;
        let sub = surface.rem_euclid(8) as usize;
        let variant = col as usize % 2;
        for y in 6..20 {
            let index = if y < row {
                0
            } else if y == row {
                12 + variant * 8 + sub
            } else {
                9
            };
            blit_sheet_tile(&mut img, tiles, index, col * 8, y * 8);
        }
        if sub < 4 && col % 2 == 0 {
            blit_sheet_tile(
                &mut img,
                tiles,
                28 + (col as usize % 4),
                col * 8,
                (row - 1) * 8,
            );
        }
        let fg_wave = (col as f32 * 0.9 + 1.1).sin() * 6.0 + (col as f32 * 0.37).cos() * 4.0;
        let fg_surface = 138 + fg_wave as i32;
        let fg_row = fg_surface / 8;
        let fg_sub = fg_surface.rem_euclid(8) as usize;
        for y in 15..20 {
            let index = if y < fg_row {
                0
            } else if y == fg_row {
                53 + fg_sub
            } else {
                52
            };
            blit_sheet_tile(&mut img, tiles, index, col * 8, y * 8);
        }
    }
    img.blit(48, 78, &crop(beaver, 0, 0, 32, 32));
    img.blit(36, 96, &crop(spray, 0, 0, 32, 16));
    img.blit(150, 36, hoop);
    img.blit(168, 44, &crop(gulls, 0, 0, 32, 16));
    img.blit(200, 58, &crop(gulls, 32, 0, 32, 16));
    img
}

fn crop(src: &Image, ox: usize, oy: usize, width: usize, height: usize) -> Image {
    let mut img = Image::new(width, height);
    for y in 0..height {
        for x in 0..width {
            img.set(
                x as i32,
                y as i32,
                src.pixels[(oy + y) * src.width + ox + x],
            );
        }
    }
    img
}

fn blit_block(dst: &mut Image, sheet: &Image, origin: i32, cols: i32, rows: i32, x: i32, y: i32) {
    for row in 0..rows {
        for col in 0..cols {
            blit_sheet_tile(
                dst,
                sheet,
                (origin + row * cols + col) as usize,
                x + col * 8,
                y + row * 8,
            );
        }
    }
}

fn blit_sheet_tile(dst: &mut Image, sheet: &Image, index: usize, dx: i32, dy: i32) {
    let ox = index * 8;
    for y in 0..8 {
        for x in 0..8 {
            let color = sheet.pixels[y * sheet.width + ox + x];
            if color != TRANSPARENT {
                dst.set(dx + x as i32, dy + y as i32, color);
            }
        }
    }
}

fn scale3(src: &Image) -> Image {
    let mut dst = Image::new(src.width * 3, src.height * 3);
    for y in 0..src.height {
        for x in 0..src.width {
            let color = src.pixels[y * src.width + x];
            for dy in 0..3 {
                for dx in 0..3 {
                    dst.set((x * 3 + dx) as i32, (y * 3 + dy) as i32, color);
                }
            }
        }
    }
    dst
}

fn font() -> Image {
    let mut sheet = Image::new(37 * 8, 8);
    let chars: Vec<u8> = b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ "
        .iter()
        .copied()
        .collect();
    for (index, ch) in chars.iter().enumerate() {
        let rows = glyph(*ch);
        for (y, row) in rows.iter().enumerate() {
            for x in 0..8 {
                if row & (0x80 >> x) != 0 {
                    sheet.set(index as i32 * 8 + x, y as i32, WHITE);
                }
            }
        }
    }
    sheet
}

pub(crate) fn glyph(ch: u8) -> [u8; 8] {
    const DIGITS: [[u8; 8]; 10] = [
        [0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00],
        [0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00],
        [0x3C, 0x66, 0x06, 0x0C, 0x30, 0x60, 0x7E, 0x00],
        [0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00],
        [0x0C, 0x1C, 0x3C, 0x6C, 0x7E, 0x0C, 0x0C, 0x00],
        [0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00],
        [0x3C, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x3C, 0x00],
        [0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00],
        [0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00],
        [0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00],
    ];
    const LETTERS: [[u8; 8]; 26] = [
        [0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00],
        [0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00],
        [0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00],
        [0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00],
        [0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0x00],
        [0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x00],
        [0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3C, 0x00],
        [0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00],
        [0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00],
        [0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38, 0x00],
        [0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00],
        [0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00],
        [0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00],
        [0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0x00],
        [0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00],
        [0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00],
        [0x3C, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x0E, 0x00],
        [0x7C, 0x66, 0x66, 0x7C, 0x78, 0x6C, 0x66, 0x00],
        [0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00],
        [0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00],
        [0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00],
        [0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00],
        [0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00],
        [0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00],
        [0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x00],
        [0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0x00],
    ];
    match ch {
        b'0'..=b'9' => DIGITS[(ch - b'0') as usize],
        b'A'..=b'Z' => LETTERS[(ch - b'A') as usize],
        _ => [0; 8],
    }
}

fn write_sfx(dir: &Path) {
    fs::create_dir_all(dir).unwrap();
    write_wav(
        &dir.join("jump.wav"),
        &tone(0.09, |t| {
            let freq = 420.0 + t * 1800.0;
            square(t, freq) * (1.0 - t / 0.09)
        }),
    );
    write_wav(
        &dir.join("hit.wav"),
        &tone(0.12, |t| {
            let noise = ((t * 31337.0).sin() * 13_001.0).sin();
            noise * (1.0 - t / 0.12).powi(2)
        }),
    );
    write_wav(
        &dir.join("ring.wav"),
        &tone(0.16, |t| {
            let env = (1.0 - t / 0.16).max(0.0);
            (sine(t, 880.0) * 0.6 + sine(t, 1320.0) * 0.4) * env
        }),
    );
    write_wav(
        &dir.join("splash.wav"),
        &tone(0.22, |t| {
            let noise = ((t * 24_133.0).sin() * 9_871.0).sin();
            let rumble = sine(t, 90.0 + 60.0 * (1.0 - t / 0.22));
            (noise * 0.55 + rumble * 0.35) * (1.0 - t / 0.22).powi(2)
        }),
    );
}

fn tone(seconds: f32, sample: impl Fn(f32) -> f32) -> Vec<i16> {
    let count = (10_512.0 * seconds) as usize;
    (0..count)
        .map(|i| {
            let t = i as f32 / 10_512.0;
            (sample(t).clamp(-1.0, 1.0) * 24_000.0) as i16
        })
        .collect()
}

fn sine(t: f32, freq: f32) -> f32 {
    (t * freq * std::f32::consts::TAU).sin()
}

fn square(t: f32, freq: f32) -> f32 {
    if sine(t, freq) >= 0.0 { 0.7 } else { -0.7 }
}

fn write_wav(path: &Path, samples: &[i16]) {
    let spec = hound::WavSpec {
        channels: 1,
        sample_rate: 10_512,
        bits_per_sample: 16,
        sample_format: hound::SampleFormat::Int,
    };
    let mut writer = hound::WavWriter::create(path, spec).unwrap();
    for sample in samples {
        writer.write_sample(*sample).unwrap();
    }
    writer.finalize().unwrap();
}
