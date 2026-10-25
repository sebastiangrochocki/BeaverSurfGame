//! Integer wave sampled once per frame into a 64-entry table that covers the 512 px period.
//! Screen Y grows downward, so a crest is a local minimum.
//!
//! All arithmetic here fits 16 bits and the C port in `gbc/src/wave.c` mirrors it line for line.

use crate::{FP, px};

/// Resting waterline of the main surfable wave, in Q10.6 screen pixels.
pub const HORIZON: i16 = 112 * FP;
/// Resting waterline of the closer foreground wave layer, in Q10.6 screen pixels.
pub const FRONT_HORIZON: i16 = 140 * FP;
/// Spatial repeat cycle of the randomized wave train, in pixels (over two screen widths).
pub const WAVE_PERIOD_PX: i16 = 512;
/// Table entries over one period, one every 8 px.
pub const TABLE_LEN: usize = 64;
/// Slope, in Q10.6, at which steering is reduced to its minimum.
pub const SLOPE_MAX: i16 = 4 * FP;

/// Pseudo-random amplitude scale (in Q8, around 256) for each 32px wave cell across the 512px period.
static CELL_AMP: [i16; 16] = [
    170, 290, 210, 350, 145, 265, 325, 185, 360, 160, 245, 310, 155, 340, 225, 275,
];

/// Pseudo-random vertical swell offset (in pixels) for each 32px wave cell across the 512px period.
static CELL_LIFT: [i8; 16] = [-4, 3, -2, 6, -5, 1, 5, -3, 7, -6, 0, 4, -4, 5, -1, -3];

/// 256 samples of sin over one turn, scaled to +-127.
static SIN8: [i8; 256] = [
    0, 3, 6, 9, 12, 16, 19, 22, 25, 28, 31, 34, 37, 40, 43, 46, 49, 51, 54, 57, 60, 63, 65, 68, 71,
    73, 76, 78, 81, 83, 85, 88, 90, 92, 94, 96, 98, 100, 102, 104, 106, 107, 109, 111, 112, 113,
    115, 116, 117, 118, 120, 121, 122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127, 127, 127,
    127, 127, 127, 126, 126, 126, 125, 125, 124, 123, 122, 122, 121, 120, 118, 117, 116, 115, 113,
    112, 111, 109, 107, 106, 104, 102, 100, 98, 96, 94, 92, 90, 88, 85, 83, 81, 78, 76, 73, 71, 68,
    65, 63, 60, 57, 54, 51, 49, 46, 43, 40, 37, 34, 31, 28, 25, 22, 19, 16, 12, 9, 6, 3, 0, -3, -6,
    -9, -12, -16, -19, -22, -25, -28, -31, -34, -37, -40, -43, -46, -49, -51, -54, -57, -60, -63,
    -65, -68, -71, -73, -76, -78, -81, -83, -85, -88, -90, -92, -94, -96, -98, -100, -102, -104,
    -106, -107, -109, -111, -112, -113, -115, -116, -117, -118, -120, -121, -122, -122, -123, -124,
    -125, -125, -126, -126, -126, -127, -127, -127, -127, -127, -127, -127, -126, -126, -126, -125,
    -125, -124, -123, -122, -122, -121, -120, -118, -117, -116, -115, -113, -112, -111, -109, -107,
    -106, -104, -102, -100, -98, -96, -94, -92, -90, -88, -85, -83, -81, -78, -76, -73, -71, -68,
    -65, -63, -60, -57, -54, -51, -49, -46, -43, -40, -37, -34, -31, -28, -25, -22, -19, -16, -12,
    -9, -6, -3,
];

pub fn tier(tick: u32) -> u8 {
    (tick / (60 * 15)).min(8) as u8
}

/// Main swell amplitude in half pixels: 12 px plus 2 px per tier.
pub fn amplitude_half(tier: u8) -> u16 {
    24 + 4 * tier.min(8) as u16
}

/// Secondary swell amplitude in half pixels: 3 px plus 1 px per tier.
pub fn amplitude2_half(tier: u8) -> u16 {
    6 + 2 * tier.min(8) as u16
}

/// Bottom-lane height as sixteenths of the top wave's swing: 5/16 to start, one more
/// sixteenth every second tier, 8/16 from tier 6. The front steepens over time on top of
/// the table amplitude it already inherits.
pub fn front_scale(tier: u8) -> i16 {
    5 + (tier.min(6) / 2) as i16
}

pub fn sin8(angle: u16) -> i16 {
    SIN8[(angle & 255) as usize] as i16
}

/// Angle in 1/256 turns. `per_half` counts half steps per pixel, so 1 spans the whole 512 px period.
fn angle(phase_px: u16, per_half: u16, tick: u32, speed: u16) -> u16 {
    (((phase_px * per_half) >> 1) + (tick as u8 as u16) * speed) & 255
}

/// Smoothly interpolated per-wave randomness: amplitude scale in Q8 and half the lift in Q10.6.
fn envelope(shifted_px: u16) -> (i16, i16) {
    let cell = ((shifted_px >> 5) & 15) as usize;
    let next = (cell + 1) & 15;
    let frac = shifted_px & 31;
    let smooth = ((frac * frac * (96 - 2 * frac)) >> 10) as i16;
    let a0 = CELL_AMP[cell];
    let a1 = CELL_AMP[next];
    let amp = a0 + (((a1 - a0) * smooth) >> 5);
    let l0 = CELL_LIFT[cell] as i16 * 32;
    let l1 = CELL_LIFT[next] as i16 * 32;
    let lift = l0 + (((l1 - l0) * smooth) >> 5);
    (amp, lift)
}

/// Table entries refreshed per tick. Entry `i` is refreshed on ticks where `tick % 4 == i % 4`,
/// so the whole table is fresh every four frames and a Game Boy can afford the work.
pub const TABLE_SLICE: usize = 16;

/// The envelope's amplitude scale (Q8) and lift (Q10.6) for one 32 px cell and a pixel inside it.
/// The C port stores these 16 x 32 values in ROM.
pub fn envelope_cell(cell: u8, frac: u8) -> (i16, i16) {
    envelope(((cell as u16 & 15) << 5) | (frac as u16 & 31))
}

/// Main wave height at table entry `index` as it stands on `tick`.
pub fn table_entry(index: usize, tick: u32, tier: u8) -> i16 {
    let shift = ((tick >> 1) & 511) as u16;
    let amp1_base = amplitude_half(tier);
    let amp2_base = amplitude2_half(tier);
    let phase = index as u16 * 8;
    let (amp_mod, lift) = envelope((phase + shift) & 511);
    let amp1 = ((amp1_base * amp_mod as u16) >> 8) as i16;
    let amp2 = ((amp2_base * (amp_mod as u16 + 256)) >> 9) as i16;
    let primary = (amp1 * sin8(angle(phase, 2, tick, 1))) >> 2;
    let secondary = (amp2 * sin8(angle(phase, 4, tick, 2))) >> 2;
    let swell = (6 * sin8(angle(phase, 1, tick, 1))) >> 2;
    HORIZON + primary + secondary + swell + lift
}

/// Fills every entry of `out` for this tick.
pub fn build_table(tick: u32, tier: u8, out: &mut [i16; TABLE_LEN]) {
    for (index, slot) in out.iter_mut().enumerate() {
        *slot = table_entry(index, tick, tier);
    }
}

/// Refreshes the quarter of the table that is due on this tick.
pub fn refresh_table(tick: u32, tier: u8, out: &mut [i16; TABLE_LEN]) {
    let quarter = (tick & 3) as usize;
    for step in 0..TABLE_SLICE {
        let index = quarter + step * 4;
        out[index] = table_entry(index, tick, tier);
    }
}

/// Top lane. The beaver starts here.
pub const LANE_TOP: u8 = 0;
/// Bottom lane: the near wave, stretched and shortened from the same height table.
pub const LANE_BOTTOM: u8 = 1;
/// World pixels added before the half-speed sample, so the bottom wave is not a copy of the top.
pub const FRONT_PHASE: i16 = 128;

/// Steer multiplier in Q8. 256 is full speed, 128 is the 50% minimum.
pub fn steer_scale(slope: i16) -> i16 {
    256 - (slope.abs().min(SLOPE_MAX) >> 1)
}

/// One frame of the main wave, sampled from the table with linear interpolation.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Sea {
    pub heights: [i16; TABLE_LEN],
    /// `front_scale` of the tier the table was last refreshed for.
    pub front_num: i16,
}

impl Sea {
    pub fn new(tick: u32, tier: u8) -> Self {
        let mut heights = [HORIZON; TABLE_LEN];
        build_table(tick, tier, &mut heights);
        Self {
            heights,
            front_num: front_scale(tier),
        }
    }

    /// Recomputes every entry for `tick`.
    pub fn rebuild(&mut self, tick: u32, tier: u8) {
        build_table(tick, tier, &mut self.heights);
        self.front_num = front_scale(tier);
    }

    /// Advances the table by one tick: a quarter of the entries catch up with `tick`.
    pub fn refresh(&mut self, tick: u32, tier: u8) {
        refresh_table(tick, tier, &mut self.heights);
        self.front_num = front_scale(tier);
    }

    /// Height at an integer world pixel.
    pub fn at_px(&self, world_px: i16) -> i16 {
        let phase = (world_px & 511) as u16;
        let index = (phase >> 3) as usize;
        let frac = (phase & 7) as i16;
        let h0 = self.heights[index];
        let h1 = self.heights[(index + 1) & (TABLE_LEN - 1)];
        h0 + (((h1 - h0) * frac) >> 3)
    }

    /// Height at a Q10.6 world position; sub-pixel bits are ignored.
    pub fn at(&self, world_x: i32) -> i16 {
        self.at_px(((world_x >> 6) & 511) as i16)
    }

    /// Bottom-lane height at an integer world pixel.
    /// `FRONT_HORIZON + ((sea((x >> 1) + FRONT_PHASE) - HORIZON) * front_num >> 4)`.
    /// Twice as long as the top wave and `front_scale` sixteenths as tall. The Game Boy
    /// front ramps slope by at most ±4 px per column; the painter clamps the rare rest.
    pub fn front_at_px(&self, world_px: i32) -> i16 {
        let sample = (world_px >> 1).wrapping_add(FRONT_PHASE as i32);
        let delta = self.at_px(sample as i16) - HORIZON;
        FRONT_HORIZON + ((delta as i32 * self.front_num as i32) >> 4) as i16
    }

    /// Bottom-lane height at a Q10.6 world position.
    pub fn front_at(&self, world_x: i32) -> i16 {
        self.front_at_px(world_x >> 6)
    }

    /// Left, here and right bottom-lane heights, 8 px apart.
    pub fn front_sample(&self, world_x: i32) -> (i16, i16, i16) {
        let px = world_x >> 6;
        (
            self.front_at_px(px - 8),
            self.front_at_px(px),
            self.front_at_px(px + 8),
        )
    }

    /// Screen-Y difference of the bottom lane across 16 pixels.
    pub fn front_slope(&self, world_x: i32) -> i16 {
        let (left, _, right) = self.front_sample(world_x);
        right - left
    }

    /// Height of `lane` at a Q10.6 world position.
    pub fn surface(&self, world_x: i32, lane: u8) -> i16 {
        if lane == LANE_BOTTOM {
            self.front_at(world_x)
        } else {
            self.at(world_x)
        }
    }

    /// Screen-Y difference across 16 pixels. Positive means the water is lower on the right.
    pub fn slope(&self, world_x: i32) -> i16 {
        self.at(world_x + px(8) as i32) - self.at(world_x - px(8) as i32)
    }

    /// Slope of whichever lane the beaver is riding.
    pub fn lane_slope(&self, world_x: i32, lane: u8) -> i16 {
        if lane == LANE_BOTTOM {
            self.front_slope(world_x)
        } else {
            self.slope(world_x)
        }
    }

    pub fn is_crest(&self, world_x: i32) -> bool {
        let here = self.at(world_x);
        here < self.at(world_x - px(8) as i32) && here < self.at(world_x + px(8) as i32)
    }

    pub fn is_trough(&self, world_x: i32) -> bool {
        let here = self.at(world_x);
        here > self.at(world_x - px(8) as i32) && here > self.at(world_x + px(8) as i32)
    }

    /// Jumps are allowed on a crest or while the face ahead rises, never in a trough.
    pub fn jump_allowed(&self, world_x: i32) -> bool {
        if self.is_trough(world_x) {
            return false;
        }
        self.is_crest(world_x) || self.slope(world_x) < 0
    }

    /// True on a crest or in the top fifth of the climb toward it.
    pub fn in_top_fifth(&self, world_x: i32) -> bool {
        if self.is_crest(world_x) {
            return true;
        }
        self.slope(world_x) < 0 && self.climb_progress(world_x) >= 204
    }

    /// How far up the current climb the water is: 0 at the trough behind, 256 at the crest ahead.
    pub fn climb_progress(&self, world_x: i32) -> i16 {
        let here = self.at(world_x);
        let crest = self.walk(world_x, 1, true);
        let trough = self.walk(world_x, -1, false);
        fraction(trough - here, trough - crest)
    }

    /// How far down from the crest behind the water is: 0 at the crest, 256 at the trough ahead.
    pub fn descent_progress(&self, world_x: i32) -> i16 {
        let here = self.at(world_x);
        let crest = self.walk(world_x, -1, true);
        let trough = self.walk(world_x, 1, false);
        fraction(here - crest, trough - crest)
    }

    /// Follows the table in 8 px steps while it keeps rising (`seek_crest`) or falling.
    fn walk(&self, world_x: i32, dir: i16, seek_crest: bool) -> i16 {
        let start = ((world_x >> 6) & 511) as i16;
        let mut prev = self.at_px(start);
        for step in 1..=20i16 {
            let next = self.at_px(start + dir * step * 8);
            let keep = if seek_crest { next < prev } else { next > prev };
            if !keep {
                break;
            }
            prev = next;
        }
        prev
    }

    /// Where the HUD arrow sits for a grounded beaver, in pixels along the 96 px bar.
    pub fn jump_meter(&self, world_x: i32) -> i8 {
        let climbing = self.jump_allowed(world_x);
        if climbing && self.in_top_fifth(world_x) {
            let top = self.climb_progress(world_x).max(204) - 204;
            (32 + (top * 32 / 52).min(31)) as i8
        } else if climbing {
            (self.climb_progress(world_x) * 32 / 204).min(31) as i8
        } else if self.is_trough(world_x) || self.slope(world_x) < 0 {
            0
        } else {
            (64 + self.descent_progress(world_x) * 32 / 257) as i8
        }
    }

    pub fn front_is_crest(&self, world_x: i32) -> bool {
        let (left, here, right) = self.front_sample(world_x);
        here < left && here < right
    }

    pub fn front_is_trough(&self, world_x: i32) -> bool {
        let (left, here, right) = self.front_sample(world_x);
        here > left && here > right
    }

    pub fn front_jump_allowed(&self, world_x: i32) -> bool {
        if self.front_is_trough(world_x) {
            return false;
        }
        self.front_is_crest(world_x) || self.front_slope(world_x) < 0
    }

    /// True on a bottom-lane crest or in the top fifth of the climb toward it.
    pub fn front_in_top_fifth(&self, world_x: i32) -> bool {
        if self.front_is_crest(world_x) {
            return true;
        }
        self.front_slope(world_x) < 0 && self.front_climb(world_x) >= 204
    }

    pub fn front_jump_meter(&self, world_x: i32) -> i8 {
        let climbing = self.front_jump_allowed(world_x);
        if climbing && self.front_in_top_fifth(world_x) {
            let top = self.front_climb(world_x).max(204) - 204;
            (32 + (top * 32 / 52).min(31)) as i8
        } else if climbing {
            (self.front_climb(world_x) * 32 / 204).min(31) as i8
        } else if self.front_is_trough(world_x) || self.front_slope(world_x) < 0 {
            0
        } else {
            (64 + self.front_descent(world_x) * 32 / 257) as i8
        }
    }

    pub fn lane_jump_allowed(&self, world_x: i32, lane: u8) -> bool {
        if lane == LANE_BOTTOM {
            self.front_jump_allowed(world_x)
        } else {
            self.jump_allowed(world_x)
        }
    }

    pub fn lane_in_top_fifth(&self, world_x: i32, lane: u8) -> bool {
        if lane == LANE_BOTTOM {
            self.front_in_top_fifth(world_x)
        } else {
            self.in_top_fifth(world_x)
        }
    }

    pub fn lane_jump_meter(&self, world_x: i32, lane: u8) -> i8 {
        if lane == LANE_BOTTOM {
            self.front_jump_meter(world_x)
        } else {
            self.jump_meter(world_x)
        }
    }

    fn front_climb(&self, world_x: i32) -> i16 {
        let here = self.front_at(world_x);
        let crest = self.front_walk(world_x, 1, true);
        let trough = self.front_walk(world_x, -1, false);
        fraction(trough - here, trough - crest)
    }

    fn front_descent(&self, world_x: i32) -> i16 {
        let here = self.front_at(world_x);
        let crest = self.front_walk(world_x, -1, true);
        let trough = self.front_walk(world_x, 1, false);
        fraction(here - crest, trough - crest)
    }

    /// Follows the bottom lane in 8 px steps while it keeps rising or falling.
    fn front_walk(&self, world_x: i32, dir: i16, seek_crest: bool) -> i16 {
        let start = world_x >> 6;
        let mut prev = self.front_at_px(start);
        for step in 1..=20i32 {
            let next = self.front_at_px(start + dir as i32 * step * 8);
            let keep = if seek_crest { next < prev } else { next > prev };
            if !keep {
                break;
            }
            prev = next;
        }
        prev
    }
}

/// `part / whole` in Q8, clamped to 0..=256, in 16-bit unsigned math.
fn fraction(part: i16, whole: i16) -> i16 {
    if whole <= 0 || part <= 0 {
        return if whole <= 0 { 256 } else { 0 };
    }
    // Half-pixel precision keeps `p << 8` inside 16 bits for the tallest waves.
    let p = (part >> 5) as u16;
    let w = (whole >> 5) as u16;
    if w == 0 {
        return 256;
    }
    ((p << 8) / w).min(256) as i16
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn height_repeats_every_period_and_adjacent_waves_differ() {
        let sea = Sea::new(17, 0);
        for x in 0..WAVE_PERIOD_PX {
            assert_eq!(sea.at_px(x), sea.at_px(x + WAVE_PERIOD_PX));
        }
        assert_ne!(sea.at_px(16), sea.at_px(16 + 256));
    }

    #[test]
    fn a_crest_is_higher_than_its_neighbors_and_a_trough_is_lower() {
        let sea = Sea::new(4, 0);
        let crest = (0..WAVE_PERIOD_PX)
            .map(|x| px(x) as i32)
            .find(|x| sea.is_crest(*x))
            .expect("crest");
        let trough = (0..WAVE_PERIOD_PX)
            .map(|x| px(x) as i32)
            .find(|x| sea.is_trough(*x))
            .expect("trough");
        assert!(sea.at(crest) < sea.at(crest - px(8) as i32));
        assert!(sea.at(crest) < sea.at(crest + px(8) as i32));
        assert!(sea.at(trough) > sea.at(trough - px(8) as i32));
        assert!(sea.at(trough) > sea.at(trough + px(8) as i32));
    }

    #[test]
    fn amplitude_grows_with_tier_and_then_caps() {
        assert!(amplitude_half(8) > amplitude_half(0));
        assert_eq!(amplitude_half(8), amplitude_half(100));
        assert_eq!(tier(0), 0);
        assert_eq!(tier(60 * 15 * 8), 8);
        assert_eq!(tier(60 * 15 * 40), 8);

        let calm = Sea::new(0, 0);
        let rough = Sea::new(0, 8);
        let span = |sea: &Sea| {
            sea.heights
                .iter()
                .map(|h| (h - HORIZON).abs())
                .max()
                .unwrap()
        };
        assert!(span(&rough) > span(&calm));
        assert!(rough.heights.iter().all(|h| *h > px(40) && *h < px(170)));
    }

    #[test]
    fn a_refresh_updates_one_quarter_and_four_refreshes_match_a_rebuild() {
        let mut sea = Sea::new(100, 2);
        let before = sea.heights;
        sea.refresh(101, 2);
        let changed = (0..TABLE_LEN)
            .filter(|i| sea.heights[*i] != before[*i])
            .collect::<std::vec::Vec<_>>();
        assert!(!changed.is_empty());
        assert!(changed.iter().all(|i| i % 4 == 1));

        let mut rolling = Sea::new(0, 0);
        for tick in 1..=4 {
            rolling.refresh(tick, 0);
        }
        for index in 0..TABLE_LEN {
            let tick = if index % 4 == 0 { 4 } else { index as u32 % 4 };
            assert_eq!(rolling.heights[index], table_entry(index, tick, 0));
        }
    }

    #[test]
    fn progress_runs_from_the_trough_to_the_crest() {
        let sea = Sea::new(9, 0);
        let crest = (0..WAVE_PERIOD_PX)
            .map(|x| px(x) as i32)
            .find(|x| sea.is_crest(*x))
            .expect("crest");
        let trough = (0..WAVE_PERIOD_PX)
            .map(|x| px(x) as i32)
            .find(|x| sea.is_trough(*x))
            .expect("trough");
        assert_eq!(sea.climb_progress(crest), 256);
        assert_eq!(sea.climb_progress(trough), 0);
        assert!(sea.in_top_fifth(crest));
        assert!(!sea.in_top_fifth(trough));
        assert!((0..=96).contains(&sea.jump_meter(crest)));
    }

    #[test]
    fn the_bottom_lane_is_the_top_wave_stretched_and_shortened() {
        assert_eq!(front_scale(0), 5);
        assert_eq!(front_scale(1), 5);
        assert_eq!(front_scale(2), 6);
        assert_eq!(front_scale(6), 8);
        assert_eq!(front_scale(8), 8);
        for tier in 0..=8 {
            let sea = Sea::new(40, tier);
            assert_eq!(sea.front_num, front_scale(tier));
            for x in 0..WAVE_PERIOD_PX {
                let sample = sea.at_px((x >> 1) + FRONT_PHASE) - HORIZON;
                let expect =
                    FRONT_HORIZON + ((sample as i32 * front_scale(tier) as i32) >> 4) as i16;
                assert_eq!(sea.front_at_px(x as i32), expect, "tier {tier} x {x}");
            }
            // A screen column is 12 world px. Front tiles slope by ±4 and the painter clamps
            // the rest, so steps past that must stay rare and never run away.
            let mut steepest = 0i16;
            let mut clamped = 0;
            for x in 0..WAVE_PERIOD_PX {
                let dy = (sea.front_at_px(x as i32 + 12) - sea.front_at_px(x as i32)).abs();
                steepest = steepest.max(dy);
                if dy > px(4) {
                    clamped += 1;
                }
            }
            assert!(
                steepest <= px(6),
                "tier {tier} front column step {steepest} is too steep to clamp"
            );
            assert!(
                clamped * 100 <= WAVE_PERIOD_PX as i32 * 8,
                "tier {tier}: {clamped} of {WAVE_PERIOD_PX} front columns need clamping"
            );
        }
    }

    #[test]
    fn the_bottom_lane_has_a_crest_and_a_meter() {
        let sea = Sea::new(11, 1);
        let crest = (0..WAVE_PERIOD_PX * 2)
            .map(|x| px(x) as i32)
            .find(|x| sea.front_is_crest(*x))
            .expect("front crest");
        assert!(sea.front_jump_allowed(crest));
        assert!(sea.front_in_top_fifth(crest));
        assert!((0..=96).contains(&sea.front_jump_meter(crest)));
        assert_eq!(sea.lane_jump_meter(crest, LANE_TOP), sea.jump_meter(crest));
        assert_eq!(
            sea.lane_jump_meter(crest, LANE_BOTTOM),
            sea.front_jump_meter(crest)
        );
    }
}
