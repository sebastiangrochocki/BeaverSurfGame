//! Backgrounds, sprites, and words. Tile indices match `tools/gen_assets`.

use agb::{
    display::{
        GraphicsFrame, Priority,
        object::Object,
        tiled::{RegularBackground, RegularBackgroundSize, TileFormat, TileSet, TileSetting},
    },
    include_aseprite, include_background_gfx,
};
use beaver_sim::{
    Banner, FLIP_FRAMES, METER_LEN, Phase, SCREEN_W, VEL, WIPEOUT_FRAMES, World, front_height, px,
};

const SCREEN_WIDTH: i32 = SCREEN_W as i32;
const METER_WIDTH: i32 = METER_LEN as i32;

/// Q10.6 world value to whole screen pixels.
fn pixels(value: i16) -> i32 {
    (value >> 6) as i32
}

include_background_gfx!(mod background, TILES => "gfx/tiles.png");

include_aseprite!(
    mod sprites,
    32x32 "gfx/beaver.png",
    32x16 "gfx/gull.png",
    "gfx/ring.png",
    32x16 "gfx/splash.png",
    32x32 "gfx/landing.png",
    32x8 "gfx/meter.png",
    16x16 "gfx/pickup.png",
    8x8 "gfx/arrow.png",
    8x8 "gfx/hearts.png",
    8x8 "gfx/font.png",
);

// Indices match the strip written by tools/gen_assets.
const SKY0: usize = 1;
const SKY1: usize = 2;
const SKY2: usize = 3;
const SKY3: usize = 4;
const CLOUD: usize = 5;
const DEEP: usize = 9;
const FAR_TOP: usize = 10;
const FAR_BODY: usize = 11;
const LINE0: usize = 12;
const SPLASH0: usize = 28;
const ISLAND: usize = 32;
const SHIP: usize = 40;
const SUN: usize = 48;
const FRONT_DEEP: usize = 52;
const FRONT_LINE0: usize = 53;
const FACE0: usize = 61;
const BODY0: usize = 65;
const ABYSS: usize = 67;
const CURL0: usize = 68;
const FRONT_FACE: usize = 70;
// Shore tiles, in the order tools/gen_assets writes them.
const SC_LAND_TOP: usize = 71;
const SC_LAND: usize = 72;
const SC_LAND_TOP_L: usize = 73;
const SC_LAND_TOP_R: usize = 74;
const SC_LAND_L: usize = 75;
const SC_LAND_R: usize = 76;
const SC_HILL6_L: usize = 77;
const SC_HILL6_R: usize = 78;
const SC_HILL7_L: usize = 79;
const SC_HILL7_R: usize = 80;
const SC_SPIRE: usize = 81;
const SC_TOWER_MID: usize = 82;
const SC_TOWER_BASE: usize = 83;
const SC_CITY_A: usize = 84;
const SC_CITY_B: usize = 85;

/// The island layer is 64 columns wide and scrolls at a third of the surf. Its rows 8-11 are
/// streamed from a scene that repeats every SCENE_COLS columns (24 surf pixels each): open
/// water with islands and ships, then a long green country with hills, a skyline and the
/// tower, then water again. Visible columns are c..c+30; the column just off the left edge
/// takes the scene column SCENE_AHEAD further on.
const SCENE_COLS: u32 = 128;
const SCENE_AHEAD: u32 = 32;
const SHORE_ROW: i32 = 8;

/// Tiles for island-layer rows 8..=11 of one scene column; 0 is clear sky. The land stands in
/// front of the horizon row so the near wave does not swallow it.
fn shore_column(col: u32) -> [usize; 4] {
    const SEA: [usize; 4] = [0, 0, 0, FAR_TOP];
    if (64..=105).contains(&col) {
        return match col {
            64 => [0, 0, SC_LAND_TOP_L, SC_LAND_L],
            105 => [0, 0, SC_LAND_TOP_R, SC_LAND_R],
            70 | 94 => [0, SC_HILL6_L, SC_HILL7_L, SC_LAND],
            71 | 95 => [0, SC_HILL6_R, SC_HILL7_R, SC_LAND],
            81 | 84 => [0, 0, SC_CITY_B, SC_LAND],
            82 | 85 => [0, 0, SC_CITY_A, SC_LAND],
            83 => [SC_SPIRE, SC_TOWER_MID, SC_TOWER_BASE, SC_LAND],
            _ => [0, 0, SC_LAND_TOP, SC_LAND],
        };
    }
    // Islands and ships are four columns wide, two rows tall, sitting on the horizon.
    let prop = |origin: usize, part: usize| [0, origin + part, origin + 4 + part, FAR_TOP];
    match col {
        3..=6 => prop(ISLAND, (col - 3) as usize),
        20..=23 => prop(ISLAND, (col - 20) as usize),
        44..=47 => prop(ISLAND, (col - 44) as usize),
        122..=125 => prop(ISLAND, (col - 122) as usize),
        10..=13 => prop(SHIP, (col - 10) as usize),
        30..=33 => prop(SHIP, (col - 30) as usize),
        54..=57 => prop(SHIP, (col - 54) as usize),
        112..=115 => prop(SHIP, (col - 112) as usize),
        _ => SEA,
    }
}

/// Rows of the two wave layers that `paint_wave` rewrites.
const NEAR_ROWS: core::ops::Range<i32> = 4..22;
const FRONT_ROWS: core::ops::Range<i32> = 14..22;

pub struct Layers {
    pub sky: RegularBackground,
    pub islands: RegularBackground,
    pub near: RegularBackground,
    pub front: RegularBackground,
    /// Tile index last written per cell. `set_tile` costs a VRAM lookup even for an
    /// unchanged tile, so the wave only writes cells whose index actually changed.
    near_map: [[u8; 32]; 32],
    front_map: [[u8; 32]; 32],
    /// Scene column under the island layer's leftmost visible column; `None` until the strip
    /// has been built.
    shore_scene: Option<u32>,
}

impl Layers {
    pub fn new() -> Self {
        let tiles = &background::TILES.tiles;
        let settings = &background::TILES.tile_settings;
        let mut sky = map(Priority::P3);
        for y in 0..32 {
            let band = match y {
                0..=2 => SKY0,
                3..=5 => SKY1,
                6..=8 => SKY2,
                _ => SKY3,
            };
            for x in 0..32 {
                put(&mut sky, tiles, settings, x, y, band);
            }
        }
        let mut islands = RegularBackground::new(
            Priority::P2,
            RegularBackgroundSize::Background64x32,
            TileFormat::FourBpp,
        );
        for y in 0..32 {
            for x in 0..64 {
                put(&mut islands, tiles, settings, x, y, 0);
            }
        }
        // Rows 4 and down: the top 32 pixels of sky stay clear behind the HUD. The sun and
        // clouds repeat on both halves of the 64-column map.
        for half in [0, 32] {
            block(&mut islands, tiles, settings, SUN, 2, 2, half + 26, 6);
            block(&mut islands, tiles, settings, CLOUD, 2, 2, half + 2, 5);
            block(&mut islands, tiles, settings, CLOUD, 2, 2, half + 9, 6);
            block(&mut islands, tiles, settings, CLOUD, 2, 2, half + 16, 4);
            block(&mut islands, tiles, settings, CLOUD, 2, 2, half + 22, 6);
        }
        for x in 0..64 {
            put(&mut islands, tiles, settings, x, 11, FAR_TOP);
            for y in 12..22 {
                put(&mut islands, tiles, settings, x, y, FAR_BODY);
            }
        }

        let mut near = map(Priority::P1);
        clear(&mut near, tiles, settings);

        let mut front = map(Priority::P0);
        clear(&mut front, tiles, settings);

        Self {
            sky,
            islands,
            near,
            front,
            near_map: [[0; 32]; 32],
            front_map: [[0; 32]; 32],
            shore_scene: None,
        }
    }

    pub fn scroll(&mut self, world: &World) {
        let scrolled = world.scroll >> 6;
        self.sky.set_scroll_pos((scrolled / 8, 0));
        self.islands.set_scroll_pos((scrolled / 3, 0));
        self.stream_shore((scrolled.max(0) as u32 / 24) % SCENE_COLS);
        paint_wave(self, world);
    }

    /// Keeps the island layer's rows 8-11 stocked as the band scrolls: one new column per
    /// column advanced, the whole strip after a reset or any other jump.
    fn stream_shore(&mut self, scene: u32) {
        let tiles = &background::TILES.tiles;
        let settings = &background::TILES.tile_settings;
        let mut write = |col: u32| {
            let x = (col % 64) as i32;
            for (row, index) in shore_column(col).into_iter().enumerate() {
                put(&mut self.islands, tiles, settings, x, SHORE_ROW + row as i32, index);
            }
        };
        match self.shore_scene {
            Some(last) if scene == last => {}
            Some(last) if scene == (last + 1) % SCENE_COLS => {
                write((scene + SCENE_AHEAD) % SCENE_COLS);
            }
            _ => {
                for i in 0..64 {
                    write((scene + SCENE_COLS - 31 + i) % SCENE_COLS);
                }
            }
        }
        self.shore_scene = Some(scene);
    }

    pub fn show(&self, frame: &mut GraphicsFrame) {
        self.sky.show(frame);
        self.islands.show(frame);
        self.near.show(frame);
        self.front.show(frame);
    }
}

pub fn draw_actors(frame: &mut GraphicsFrame, world: &World, new_best: bool) {
    if world.phase == Phase::Play {
        let flap = ((world.tick / 10) % 2) as usize;
        for gull in world.gulls.into_iter().flatten() {
            Object::new(sprites::GULL.sprite(flap))
                .set_priority(Priority::P0)
                .set_pos((pixels(gull.x) - 16, pixels(gull.y) - 8))
                .show(frame);
        }
        for ring in world.rings.into_iter().flatten() {
            Object::new(sprites::RING.sprite(0))
                .set_priority(Priority::P0)
                .set_pos((pixels(ring.x) - 16, pixels(ring.y) - 16))
                .show(frame);
        }
        if let Some(heart) = world.heart_pickup {
            let glow = ((world.tick / 8) % 2) as usize;
            let bob = if (world.tick / 16) % 2 == 0 { 0 } else { 1 };
            Object::new(sprites::PICKUP.sprite(glow))
                .set_priority(Priority::P0)
                .set_pos((pixels(heart.x) - 8, pixels(heart.y) - 8 + bob))
                .show(frame);
        }
        draw_hearts(frame, world.hearts);
        draw_number(frame, world.score, SCREEN_WIDTH - 8 * 6 - 4, 4);
        draw_meter(frame, world);
        let mut text = [b' '; 16];
        let len = banner_text(world.banner, &mut text);
        if len > 0 {
            if world.banner_timer > 8 || world.banner_timer % 2 == 0 {
                let x = (SCREEN_WIDTH - len as i32 * 8) / 2;
                draw_text(frame, &text[..len], x, 24);
            }
        } else if world.combo >= 2 && world.combo_timer > 90 {
            draw_text(frame, b"COMBO X", 88, 24);
            draw_text(frame, &[b'0' + world.combo], 144, 24);
        }
    }

    let blink = world.invuln > 0 && (world.invuln / 4) % 2 == 0;
    let wiping = world.phase == Phase::Play && world.stun > 0 && world.banner == Banner::Wipeout;
    let x = pixels(world.beaver.x) - 16;
    let y = pixels(world.beaver.y) - 28 + bob(world);
    if wiping {
        // Tumble off upside down and sink, leave the board floating, then climb back on.
        let gone = (WIPEOUT_FRAMES - world.stun) as i32;
        draw_wake(frame, world);
        Object::new(sprites::BEAVER.sprite(7))
            .set_priority(Priority::P0)
            .set_pos((x, y))
            .show(frame);
        if gone < 16 {
            Object::new(sprites::BEAVER.sprite(5))
                .set_priority(Priority::P0)
                .set_pos((x - gone / 2, y + gone))
                .show(frame);
        } else if gone >= 48 && !blink {
            let rise = (WIPEOUT_FRAMES as i32 - gone).min(8);
            Object::new(sprites::BEAVER.sprite(0))
                .set_priority(Priority::P0)
                .set_pos((x, y + rise))
                .show(frame);
        }
    } else if world.phase != Phase::Play || !blink {
        draw_wake(frame, world);
        let sprite = sprites::BEAVER.sprite(beaver_frame(world));
        Object::new(sprite)
            .set_priority(Priority::P0)
            .set_pos((x, y))
            .show(frame);
    }
    if world.phase == Phase::Play {
        draw_landing(frame, world);
    }

    match world.phase {
        Phase::Title => {
            draw_text(frame, b"BEAVER", 96, 28);
            draw_text(frame, b"SURF", 104, 40);
            draw_text(frame, b"BEST", 80, 64);
            draw_number(frame, world.best, 116, 64);
            if (world.tick / 30) % 2 == 0 {
                draw_text(frame, b"PRESS START", 76, 144);
            }
        }
        Phase::Over => {
            draw_text(frame, b"WIPED OUT", 84, 36);
            draw_text(frame, b"SCORE", 72, 52);
            draw_number(frame, world.score, 120, 52);
            draw_text(frame, b"BEST", 80, 64);
            draw_number(frame, world.best, 116, 64);
            if new_best {
                draw_text(frame, b"NEW BEST", 88, 80);
            }
            draw_hearts(frame, 0);
        }
        Phase::Play => {}
    }
}

pub fn install_palettes(gfx: &mut agb::display::Graphics) {
    gfx.set_background_palettes(background::PALETTES);
}

fn paint_wave(layers: &mut Layers, world: &World) {
    let tiles = &background::TILES.tiles;
    let settings = &background::TILES.tile_settings;
    let tier = world.wave_tier();
    for col in 0..32i32 {
        let world_x = world.scroll + px(col as i16 * 8) as i32;
        let surface = pixels(world.sea.at(world_x)).clamp(48, 152);
        let row = surface / 8;
        let sub = surface.rem_euclid(8) as usize;
        let variant = ((world.scroll >> 9) + col).rem_euclid(2) as usize;
        let left = world.sea.at(world_x - px(8) as i32);
        let right = world.sea.at(world_x + px(8) as i32);
        let breaking = (right - left).abs() > px(2);
        let fleck = (col as u32 + world.tick / 6) % 3 == 0;
        let steep = (right - left).abs() > px(4);
        let spray = if row > NEAR_ROWS.start && sub < 4 && (steep || breaking || fleck) {
            Some(if steep {
                CURL0 + ((col as u32 / 2 + world.tick / 8) as usize % 2)
            } else if breaking {
                SPLASH0 + 2 + ((col as u32 + world.tick / 4) as usize % 2)
            } else {
                SPLASH0 + (col as usize + world.tick as usize / 5) % 2
            })
        } else {
            None
        };
        let face_phase = (col as u32 + world.tick / 6) as usize % 4;
        let body_phase = (col as u32 / 2 + world.tick / 14) as usize % 2;
        for y in NEAR_ROWS {
            let index = if y == row {
                LINE0 + variant * 8 + sub
            } else if y + 1 == row {
                spray.unwrap_or(0)
            } else if y < row {
                0
            } else if y == row + 1 {
                FACE0 + face_phase
            } else if y == row + 2 {
                BODY0 + body_phase
            } else if y <= row + 4 {
                DEEP
            } else {
                ABYSS
            };
            put_cached(
                &mut layers.near,
                &mut layers.near_map,
                tiles,
                settings,
                col,
                y,
                index,
            );
        }

        let front_px = (((world.scroll * 3 / 2) >> 6) + col * 8) as u16;
        let front_surface = pixels(front_height(front_px, world.tick, tier)).clamp(116, 156);
        let front_row = front_surface / 8;
        let front_sub = front_surface.rem_euclid(8) as usize;
        for y in FRONT_ROWS {
            let index = if y < front_row {
                0
            } else if y == front_row {
                FRONT_LINE0 + front_sub
            } else if y == front_row + 1 {
                FRONT_FACE
            } else {
                FRONT_DEEP
            };
            put_cached(
                &mut layers.front,
                &mut layers.front_map,
                tiles,
                settings,
                col,
                y,
                index,
            );
        }
    }
}

fn draw_wake(frame: &mut GraphicsFrame, world: &World) {
    let feet = world.beaver.y;
    let surface = world.sea.at(world.beaver_world_x());
    if world.beaver.airborne && (feet - surface).abs() > px(12) {
        return;
    }
    let fast = world.speed >= VEL * 5 / 2;
    let which = if world.beaver.airborne || (fast && world.tick % 4 < 3) {
        1
    } else {
        ((world.tick / 7) % 2) as usize
    };
    Object::new(sprites::SPLASH.sprite(which))
        .set_priority(Priority::P0)
        .set_pos((pixels(world.beaver.x) - 16, pixels(feet) - 10))
        .show(frame);
}

/// Fills `out` with the banner line and returns its length.
fn banner_text(banner: Banner, out: &mut [u8; 16]) -> usize {
    let mut len = 0;
    let mut push = |bytes: &[u8], len: &mut usize| {
        for byte in bytes {
            if *len < out.len() {
                out[*len] = *byte;
                *len += 1;
            }
        }
    };
    match banner {
        Banner::None => {}
        Banner::Wipeout => push(b"WIPEOUT", &mut len),
        Banner::Heart => push(b"HALF HEART", &mut len),
        Banner::Trick { points, mult, air } => {
            push(if air { b"AIR +" } else { b"FLIP +" }, &mut len);
            let mut digits = [0u8; 6];
            let mut count = 0;
            let mut value = points;
            loop {
                digits[count] = b'0' + (value % 10) as u8;
                count += 1;
                value /= 10;
                if value == 0 || count == digits.len() {
                    break;
                }
            }
            for index in (0..count).rev() {
                push(&digits[index..=index], &mut len);
            }
            if mult > 1 {
                push(b" X", &mut len);
                push(&[b'0' + mult.min(9)], &mut len);
            }
        }
    }
    len
}

fn draw_meter(frame: &mut GraphicsFrame, world: &World) {
    let left = (SCREEN_WIDTH - METER_WIDTH) / 2;
    if world.meter >= 0 {
        Object::new(sprites::ARROW.sprite(0))
            .set_priority(Priority::P0)
            .set_pos((left + world.meter as i32 - 4, 3))
            .show(frame);
    }
    for piece in 0..3 {
        Object::new(sprites::METER.sprite(piece))
            .set_priority(Priority::P0)
            .set_pos((left + piece as i32 * 32, 11))
            .show(frame);
    }
}

fn draw_landing(frame: &mut GraphicsFrame, world: &World) {
    let Some(splash) = world.splash else {
        return;
    };
    let x = pixels(splash.x) - 16;
    let y = pixels(splash.y);
    if splash.big {
        Object::new(sprites::LANDING.sprite(splash.age as usize / 7))
            .set_priority(Priority::P0)
            .set_pos((x, y - 30))
            .show(frame);
    } else if splash.age < 10 {
        Object::new(sprites::SPLASH.sprite(1))
            .set_priority(Priority::P0)
            .set_pos((x, y - 12))
            .show(frame);
    }
}

fn beaver_frame(world: &World) -> usize {
    if world.phase == Phase::Title {
        return 0;
    }
    if world.beaver.airborne {
        if world.beaver.flip > 0 {
            let turned = (FLIP_FRAMES - world.beaver.flip) as usize * 4 / FLIP_FRAMES as usize;
            return if turned == 0 { 3 } else { 3 + turned };
        }
        return if world.beaver.vy <= VEL { 3 } else { 2 };
    }
    let tilt = world.sea.slope(world.beaver_world_x());
    if tilt < -px(2) {
        1
    } else if tilt > px(2) {
        2
    } else {
        0
    }
}

fn bob(world: &World) -> i32 {
    if world.phase != Phase::Title {
        return 0;
    }
    let t = (world.tick % 24) as i32;
    if t < 12 { t / 4 } else { (24 - t) / 4 }
}

fn draw_hearts(frame: &mut GraphicsFrame, halves: u8) {
    for index in 0..3 {
        let filled = halves.saturating_sub(index * 2);
        let sprite = if filled >= 2 {
            0
        } else if filled == 1 {
            1
        } else {
            2
        };
        Object::new(sprites::HEARTS.sprite(sprite))
            .set_priority(Priority::P0)
            .set_pos((4 + index as i32 * 10, 4))
            .show(frame);
    }
}

fn draw_number(frame: &mut GraphicsFrame, mut value: u32, x: i32, y: i32) {
    let mut digits = [b'0'; 6];
    for slot in digits.iter_mut().rev() {
        *slot = b'0' + (value % 10) as u8;
        value /= 10;
    }
    draw_text(frame, &digits, x, y);
}

fn draw_text(frame: &mut GraphicsFrame, text: &[u8], x: i32, y: i32) {
    for (index, ch) in text.iter().enumerate() {
        Object::new(sprites::FONT.sprite(glyph_index(*ch)))
            .set_priority(Priority::P0)
            .set_pos((x + index as i32 * 8, y))
            .show(frame);
    }
}

fn glyph_index(ch: u8) -> usize {
    match ch {
        b'0'..=b'9' => (ch - b'0') as usize,
        b'A'..=b'Z' => 10 + (ch - b'A') as usize,
        _ => 36,
    }
}

fn map(priority: Priority) -> RegularBackground {
    RegularBackground::new(
        priority,
        RegularBackgroundSize::Background32x32,
        TileFormat::FourBpp,
    )
}

fn fill(bg: &mut RegularBackground, tiles: &TileSet, settings: &[TileSetting], index: usize) {
    for y in 0..32 {
        for x in 0..32 {
            put(bg, tiles, settings, x, y, index);
        }
    }
}

fn clear(bg: &mut RegularBackground, tiles: &TileSet, settings: &[TileSetting]) {
    fill(bg, tiles, settings, 0);
}

fn block(
    bg: &mut RegularBackground,
    tiles: &TileSet,
    settings: &[TileSetting],
    origin: usize,
    cols: i32,
    rows: i32,
    x: i32,
    y: i32,
) {
    for row in 0..rows {
        for col in 0..cols {
            put(
                bg,
                tiles,
                settings,
                x + col,
                y + row,
                origin + (row * cols + col) as usize,
            );
        }
    }
}

#[allow(clippy::too_many_arguments)]
fn put_cached(
    bg: &mut RegularBackground,
    map: &mut [[u8; 32]; 32],
    tiles: &TileSet,
    settings: &[TileSetting],
    x: i32,
    y: i32,
    index: usize,
) {
    let cell = &mut map[y as usize][x as usize];
    if *cell as usize != index {
        *cell = index as u8;
        put(bg, tiles, settings, x, y, index);
    }
}

fn put(
    bg: &mut RegularBackground,
    tiles: &TileSet,
    settings: &[TileSetting],
    x: i32,
    y: i32,
    index: usize,
) {
    bg.set_tile((x, y), tiles, settings[index]);
}
