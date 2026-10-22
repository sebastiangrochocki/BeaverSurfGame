//! Surfing rules shared by the Game Boy Advance game, the Game Boy Color port, and the host tests.
//!
//! Arithmetic contract, mirrored exactly by `gbc/src/rules.c`:
//! - Positions, heights and screen coordinates are `i16` in Q10.6: 64 is one pixel.
//! - Velocities and the surf speed are `i16` in Q8.8: 256 is one pixel per frame.
//!   A position advances by `velocity >> 2`.
//! - Any product that can exceed 16 bits is written with an explicit `i32` widening.
//! - Division truncates toward zero; `>>` on a signed value is an arithmetic shift.
//! - Only `tick`, `scroll`, `score` and `best` are 32-bit.

#![cfg_attr(not(test), no_std)]

mod save;
pub mod trace;
mod wave;

pub use save::{decode_save, encode_save};
pub use wave::{
    FRONT_HORIZON, HORIZON, SLOPE_MAX, Sea, TABLE_LEN, TABLE_SLICE, WAVE_PERIOD_PX, amplitude_half,
    envelope_cell, front_height, steer_scale, table_entry, tier,
};

/// One pixel in position units (Q10.6).
pub const FP: i16 = 64;
/// One pixel per frame in velocity units (Q8.8).
pub const VEL: i16 = 256;
pub const SCREEN_W: i16 = 240;
pub const SCREEN_H: i16 = 160;

pub const LANE_MIN: i16 = 20 * FP;
pub const LANE_MAX: i16 = 212 * FP;
pub const BEAVER_X0: i16 = 64 * FP;

/// Cruising surf speed. Surf speed drifts back here.
pub const SCROLL_SPEED: i16 = VEL;
pub const MIN_SPEED: i16 = 3 * VEL / 4;
/// Top speed before combo; each combo step raises it.
pub const MAX_SPEED: i16 = 3 * VEL;
/// Most extra speed one frame of slope can add or remove.
const SLOPE_ACCEL_MAX: i16 = 8;
/// Extra speed per frame while pumping a downhill face.
const PUMP_ACCEL: i16 = 6;

/// Steering builds up over about 15 frames so a tap nudges and a hold swims.
pub const STEER_ACCEL: i16 = VEL / 6;
pub const STEER_MAX: i16 = 5 * VEL / 2;

pub const GRAVITY: i16 = 51;
pub const DIVE_GRAVITY: i16 = 154;
pub const JUMP_VY: i16 = -(VEL * 4);
pub const EARLY_JUMP_VY: i16 = -(VEL * 2);
/// Extra lift when right is held at take-off.
const RIGHT_TUG_VY: i16 = VEL / 2;
const MAX_JUMP_VY: i16 = -(VEL * 7);

/// Jump-timing bar: 0 is the foot of the climb, the green zone is the top-fifth jump window,
/// and past it the crest has gone by.
pub const METER_LEN: i16 = 96;
pub const METER_GREEN: (i8, i8) = (32, 64);
/// Most pixels the HUD arrow moves per frame, so it glides instead of jumping.
pub const METER_RATE: i8 = 3;

/// Airborne frames one backflip takes.
pub const FLIP_FRAMES: u8 = 26;
const FLIP_POINTS: u16 = 20;
const FLIP_SPEED: i16 = VEL / 2;
/// Air frames per point, banked only for jumps launched from the green window.
const AIR_FRAMES_PER_POINT: u16 = 3;
/// Frames of the wipeout sequence: the beaver tumbles off, is gone, then climbs back on.
pub const WIPEOUT_FRAMES: u8 = 64;
/// A heart pickup rides beside every third ring while the beaver is hurt.
const HEART_EVERY_RINGS: u16 = 3;
const HEART_GAP: i16 = 44 * FP;
const BANNER_FRAMES: u8 = 50;
/// Combo level at which time score doubles.
pub const FLOW_COMBO: u8 = 3;

pub const MAX_COMBO: u8 = 4;
/// Grounded frames before an unbroken ramp-to-ramp chain resets.
const COMBO_FRAMES: u16 = 150;
const SPLASH_FRAMES: u8 = 21;

pub const START_HEARTS: u8 = 6;
pub const INVULN_FRAMES: u8 = 45;
pub const RING_POINTS: u32 = 50;
/// Gull airspeed toward the left, on top of the surf scroll.
const GULL_AIR_SPEED: i16 = FP;
/// Gulls spawn on any 2-pixel lane in this band and glide between its edges. The top sits just
/// under the HUD; the bottom skims the water, so a beaver on a crest has to duck into a trough
/// or jump over.
pub const GULL_Y_MIN: i16 = 48 * FP;
pub const GULL_Y_MAX: i16 = 80 * FP;
const GULL_LANES: u16 = ((GULL_Y_MAX - GULL_Y_MIN) / (2 * FP)) as u16 + 1;
/// Vertical glide step per tick, in eighths of a pixel: -4..=4. A steep glide starts at the far
/// edge of the band so the bird crosses it corner to corner.
const GULL_GLIDE: i16 = FP / 8;
const GULL_STEEP: i16 = 2;
/// Zig-zag flight: this glide, reversed every GULL_ZIG_TICKS ticks (about six pixels a leg).
const GULL_ZIG: i16 = 4;
const GULL_ZIG_TICKS: u8 = 14;
/// Lanes a steep glide may start in, counted from the edge it leaves.
const GULL_EDGE_LANES: i16 = 5;
/// From this tier gulls come in pairs that cross each other.
const GULL_PAIR_TIER: u8 = 2;
/// Trailing gull of a pair flies this far behind the leader.
const GULL_PAIR_GAP: i16 = 36 * FP;
/// Ring center sits this far above the water, so a jump is what passes through it.
const RING_LIFT: i16 = 40 * FP;
pub const GULL_SLOTS: usize = 4;
pub const RING_SLOTS: usize = 3;

pub fn px(pixels: i16) -> i16 {
    pixels * FP
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Input {
    pub left: bool,
    pub right: bool,
    pub jump: bool,
    /// In the air: drop faster onto the next downhill face. On the water: hug a downhill face for speed.
    pub dive: bool,
    pub start: bool,
}

impl Input {
    pub const LEFT: u8 = 1;
    pub const RIGHT: u8 = 2;
    pub const JUMP: u8 = 4;
    pub const DIVE: u8 = 8;
    pub const START: u8 = 16;

    pub fn to_bits(self) -> u8 {
        (self.left as u8) * Self::LEFT
            | (self.right as u8) * Self::RIGHT
            | (self.jump as u8) * Self::JUMP
            | (self.dive as u8) * Self::DIVE
            | (self.start as u8) * Self::START
    }

    pub fn from_bits(bits: u8) -> Self {
        Self {
            left: bits & Self::LEFT != 0,
            right: bits & Self::RIGHT != 0,
            jump: bits & Self::JUMP != 0,
            dive: bits & Self::DIVE != 0,
            start: bits & Self::START != 0,
        }
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Events {
    pub jumped: bool,
    pub landed: bool,
    pub clean_landing: bool,
    /// Landed with points banked from air time or flips.
    pub trick: bool,
    /// Landed mid-flip.
    pub wipeout: bool,
    pub hit: bool,
    pub ring: bool,
    pub heart: bool,
    pub died: bool,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Phase {
    Title,
    Play,
    Over,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Banner {
    #[default]
    None,
    /// Points banked on landing: air time and flips, times the multiplier.
    Trick {
        points: u16,
        mult: u8,
        air: bool,
    },
    Wipeout,
    Heart,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Beaver {
    pub x: i16,
    pub y: i16,
    pub vx: i16,
    pub vy: i16,
    pub airborne: bool,
    /// Frames left in the current backflip; 0 when upright.
    pub flip: u8,
    /// Backflips completed since take-off.
    pub flips: u8,
    /// Launched from the green window, so air time counts.
    pub perfect: bool,
    pub air_time: u16,
    /// Rings passed through since take-off.
    pub rings: u8,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Actor {
    pub x: i16,
    pub y: i16,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Splash {
    pub x: i16,
    pub y: i16,
    pub age: u8,
    pub big: bool,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct World {
    pub tick: u32,
    /// World scroll in Q10.6.
    pub scroll: i32,
    /// Forward surf speed in Q8.8.
    pub speed: i16,
    pub beaver: Beaver,
    pub gulls: [Option<Actor>; GULL_SLOTS],
    /// Vertical speed of each gull slot, Q10.6 per tick.
    pub gull_vy: [i16; GULL_SLOTS],
    /// Ticks until a zig-zagging gull reverses; 0 for gulls that fly straight or glide.
    pub gull_turn: [u8; GULL_SLOTS],
    pub rings: [Option<Actor>; RING_SLOTS],
    pub heart_pickup: Option<Actor>,
    pub ring_count: u16,
    pub splash: Option<Splash>,
    pub combo: u8,
    pub combo_timer: u16,
    /// Jump-timing arrow position on the HUD bar, held at its take-off value while airborne.
    /// Negative until the first grounded frame.
    pub meter: i8,
    pub stun: u8,
    pub banner: Banner,
    pub banner_timer: u8,
    pub hearts: u8,
    pub score: u32,
    pub best: u32,
    pub phase: Phase,
    pub invuln: u8,
    pub gull_timer: u16,
    pub ring_timer: u16,
    /// Xorshift state for gull lanes, stirred with the beaver's position at each spawn so two
    /// runs only match when the inputs do.
    pub rng: u16,
    /// The main wave for the current tick.
    pub sea: Sea,
    start_held: bool,
    jump_held: bool,
}

impl World {
    pub fn new() -> Self {
        let mut world = Self {
            tick: 0,
            scroll: 0,
            speed: SCROLL_SPEED,
            beaver: Beaver {
                x: BEAVER_X0,
                y: HORIZON,
                vx: 0,
                vy: 0,
                airborne: false,
                flip: 0,
                flips: 0,
                perfect: false,
                air_time: 0,
                rings: 0,
            },
            gulls: [None; GULL_SLOTS],
            gull_vy: [0; GULL_SLOTS],
            gull_turn: [0; GULL_SLOTS],
            rings: [None; RING_SLOTS],
            heart_pickup: None,
            ring_count: 0,
            splash: None,
            combo: 0,
            combo_timer: 0,
            meter: -1,
            stun: 0,
            banner: Banner::None,
            banner_timer: 0,
            hearts: START_HEARTS,
            score: 0,
            best: 0,
            phase: Phase::Title,
            invuln: 0,
            gull_timer: gull_interval(0),
            ring_timer: ring_interval(0),
            rng: RNG_SEED,
            sea: Sea::new(0, 0),
            start_held: false,
            jump_held: false,
        };
        world.beaver.y = world.sea.at(world.beaver_world_x());
        world
    }

    /// Difficulty tier used for the wave: the title screen stays calm however long it waits.
    pub fn wave_tier(&self) -> u8 {
        match self.phase {
            Phase::Title => 0,
            Phase::Play | Phase::Over => tier(self.tick),
        }
    }

    pub fn beaver_world_x(&self) -> i32 {
        self.scroll + self.beaver.x as i32
    }

    pub fn step(&mut self, input: Input) -> Events {
        let start_edge = input.start && !self.start_held;
        self.start_held = input.start;

        match self.phase {
            Phase::Title => {
                self.tick = self.tick.wrapping_add(1);
                self.scroll = self.scroll.wrapping_add((SCROLL_SPEED >> 2) as i32);
                if start_edge {
                    let best = self.best;
                    *self = Self::new();
                    self.best = best;
                    self.phase = Phase::Play;
                    self.start_held = input.start;
                }
                self.sea.refresh(self.tick, 0);
                self.beaver.y = self.sea.at(self.beaver_world_x());
                Events::default()
            }
            Phase::Over => {
                if start_edge {
                    self.phase = Phase::Title;
                    self.tick = 0;
                    self.sea.rebuild(self.tick, 0);
                }
                Events::default()
            }
            Phase::Play => self.simulate(input),
        }
    }

    fn simulate(&mut self, input: Input) -> Events {
        let mut events = Events::default();
        self.tick = self.tick.wrapping_add(1);
        self.sea.refresh(self.tick, tier(self.tick));
        let scroll_step = self.speed >> 2;
        self.scroll = self.scroll.wrapping_add(scroll_step as i32);
        if self.tick & 3 == 0 {
            let flow = if self.combo >= FLOW_COMBO { 2 } else { 1 };
            self.score = self.score.saturating_add(flow);
        }
        let jump_edge = input.jump && !self.jump_held;
        self.jump_held = input.jump;
        if self.banner_timer > 0 {
            self.banner_timer -= 1;
            if self.banner_timer == 0 {
                self.banner = Banner::None;
            }
        }

        let stunned = self.stun > 0;
        if stunned {
            self.stun -= 1;
        }
        self.steer(if stunned { Input::default() } else { input });

        let world_x = self.beaver_world_x();
        let surface = self.sea.at(world_x);
        let face = self.sea.slope(world_x);

        if self.beaver.airborne {
            if jump_edge && self.beaver.flip == 0 {
                self.beaver.flip = FLIP_FRAMES;
            }
            if self.beaver.flip > 0 {
                self.beaver.flip -= 1;
                if self.beaver.flip == 0 {
                    self.beaver.flips = self.beaver.flips.saturating_add(1);
                }
            }
            self.beaver.air_time = self.beaver.air_time.saturating_add(1);
            self.beaver.vy += if input.dive { DIVE_GRAVITY } else { GRAVITY };
            self.beaver.y += self.beaver.vy >> 2;
            if self.beaver.y >= surface && self.beaver.vy > 0 {
                self.beaver.y = surface;
                self.beaver.vy = 0;
                self.beaver.airborne = false;
                self.land(face, &mut events);
            }
        } else {
            self.beaver.y = surface;
            self.beaver.vy = 0;
            self.ride(face, input);
            self.glide_meter(self.sea.jump_meter(world_x));
            if input.jump && !stunned && self.sea.jump_allowed(world_x) {
                self.launch(world_x, input);
                events.jumped = true;
            }
        }

        self.speed = self.speed.clamp(MIN_SPEED, self.max_speed());
        self.age_splash(scroll_step);
        self.move_actors(scroll_step);
        if self.invuln > 0 {
            self.invuln -= 1;
        } else if self.phase == Phase::Play && self.touching_gull() {
            events.hit = true;
            self.lose_half_heart(&mut events);
        }
        if self.through_ring() {
            self.score = self.score.saturating_add(RING_POINTS);
            events.ring = true;
            if self.beaver.airborne {
                self.beaver.rings = self.beaver.rings.saturating_add(1);
            }
        }
        if self.touching_heart() {
            self.heart_pickup = None;
            self.hearts = (self.hearts + 1).min(START_HEARTS);
            self.show(Banner::Heart);
            events.heart = true;
        }
        events
    }

    fn lose_half_heart(&mut self, events: &mut Events) {
        self.hearts = self.hearts.saturating_sub(1);
        self.invuln = self.invuln.max(INVULN_FRAMES);
        if self.hearts == 0 {
            self.phase = Phase::Over;
            events.died = true;
            if self.score > self.best {
                self.best = self.score;
            }
        }
    }

    pub fn max_speed(&self) -> i16 {
        MAX_SPEED + self.combo as i16 * (VEL / 3)
    }

    fn glide_meter(&mut self, target: i8) {
        self.meter = if self.meter < 0 {
            target
        } else {
            self.meter + (target - self.meter).clamp(-METER_RATE, METER_RATE)
        };
    }

    /// Screen-space swimming with momentum: gradual to build, quick to reverse.
    fn steer(&mut self, input: Input) {
        let dir: i16 = match (input.left, input.right) {
            (true, false) => -1,
            (false, true) => 1,
            _ => 0,
        };
        if dir == 0 {
            self.beaver.vx = self.beaver.vx * 3 / 4;
            if self.beaver.vx.abs() < VEL / 16 {
                self.beaver.vx = 0;
            }
        } else {
            let scale = if self.beaver.airborne {
                160
            } else {
                steer_scale(self.sea.slope(self.beaver_world_x()))
            };
            // Building speed is gradual and slope-dependent; braking into a reversal is not.
            let accel = if self.beaver.vx * dir < 0 {
                STEER_ACCEL * 3
            } else {
                ((STEER_ACCEL as i32 * scale as i32) >> 8) as i16
            };
            self.beaver.vx = (self.beaver.vx + dir * accel).clamp(-STEER_MAX, STEER_MAX);
        }
        self.beaver.x += self.beaver.vx >> 2;
        if self.beaver.x <= LANE_MIN || self.beaver.x >= LANE_MAX {
            self.beaver.x = self.beaver.x.clamp(LANE_MIN, LANE_MAX);
            self.beaver.vx = 0;
        }
    }

    /// Downhill water adds surf speed and uphill water takes it away.
    fn ride(&mut self, face: i16, input: Input) {
        let mut accel = (face / 10).clamp(-SLOPE_ACCEL_MAX, SLOPE_ACCEL_MAX);
        if face < 0 && input.right {
            accel = accel * 3 / 4;
        }
        if face > 0 && (input.right || input.dive) {
            accel += PUMP_ACCEL;
        }
        self.speed += accel + (SCROLL_SPEED - self.speed) / 40;
        if self.combo_timer > 0 {
            self.combo_timer -= 1;
            if self.combo_timer == 0 {
                self.combo = 0;
            }
        }
    }

    /// Take-off height grows with surf speed, so speed from the last ramp carries into the next.
    fn launch(&mut self, world_x: i32, input: Input) {
        let excess = (self.speed - SCROLL_SPEED).max(0);
        let tug = if input.right { RIGHT_TUG_VY } else { 0 };
        self.beaver.perfect = self.sea.in_top_fifth(world_x);
        self.beaver.air_time = 0;
        self.beaver.rings = 0;
        if self.beaver.perfect {
            self.beaver.vy = JUMP_VY - excess * 3 / 8 - tug;
            self.speed += VEL / 2;
        } else {
            self.beaver.vy = EARLY_JUMP_VY - excess / 8 - tug / 4;
            self.speed = self.speed * 3 / 4;
        }
        self.beaver.vy = self.beaver.vy.max(MAX_JUMP_VY);
        self.beaver.airborne = true;
        self.beaver.y += self.beaver.vy >> 2;
    }

    /// Landing on a downhill face is a clean landing: a speed kick and a growing combo.
    fn land(&mut self, face: i16, events: &mut Events) {
        events.landed = true;
        let flips = self.beaver.flips;
        let mid_flip = self.beaver.flip > 0;
        self.beaver.flip = 0;
        self.beaver.flips = 0;
        if mid_flip {
            events.wipeout = true;
            self.combo = 0;
            self.combo_timer = 0;
            self.speed = MIN_SPEED;
            self.stun = WIPEOUT_FRAMES;
            self.show(Banner::Wipeout);
            self.splash = Some(Splash {
                x: self.beaver.x,
                y: self.beaver.y,
                age: 0,
                big: true,
            });
            self.lose_half_heart(events);
            self.invuln = self.invuln.max(WIPEOUT_FRAMES + INVULN_FRAMES);
            return;
        }
        let air_points = if self.beaver.perfect {
            self.beaver.air_time / AIR_FRAMES_PER_POINT
        } else {
            0
        };
        let base = air_points + FLIP_POINTS * flips as u16;
        if base > 0 {
            let mult = 1 + flips + self.beaver.rings;
            let points = base * mult as u16;
            events.trick = true;
            self.speed += FLIP_SPEED * flips as i16;
            self.score = self.score.saturating_add(points as u32);
            self.show(Banner::Trick {
                points,
                mult,
                air: air_points > 0,
            });
        }
        let clean = face > FP / 2;
        if clean {
            self.combo = (self.combo + 1).min(MAX_COMBO);
            self.speed += VEL + self.combo as i16 * (VEL / 4);
            self.score = self.score.saturating_add(10 * self.combo as u32);
            events.clean_landing = true;
        } else if face < -FP {
            self.combo = 0;
            self.speed = self.speed * 4 / 5;
        } else {
            self.speed += VEL / 6;
        }
        self.combo_timer = COMBO_FRAMES;
        self.splash = Some(Splash {
            x: self.beaver.x,
            y: self.beaver.y,
            age: 0,
            big: clean,
        });
    }

    fn show(&mut self, banner: Banner) {
        self.banner = banner;
        self.banner_timer = BANNER_FRAMES;
    }

    fn age_splash(&mut self, scroll_step: i16) {
        if let Some(splash) = self.splash.as_mut() {
            splash.age += 1;
            splash.x -= scroll_step;
            if splash.age >= SPLASH_FRAMES {
                self.splash = None;
            }
        }
    }

    fn move_actors(&mut self, scroll_step: i16) {
        for ((slot, vy), turn) in self
            .gulls
            .iter_mut()
            .zip(self.gull_vy.iter_mut())
            .zip(self.gull_turn.iter_mut())
        {
            if let Some(gull) = slot {
                gull.x -= scroll_step + GULL_AIR_SPEED;
                if gull.x < px(-24) {
                    *slot = None;
                    *vy = 0;
                    *turn = 0;
                } else if *vy != 0 {
                    if *turn != 0 {
                        *turn -= 1;
                        if *turn == 0 {
                            *turn = GULL_ZIG_TICKS;
                            *vy = -*vy;
                        }
                    }
                    gull.y += *vy;
                    if gull.y < GULL_Y_MIN {
                        gull.y = GULL_Y_MIN;
                        *vy = -*vy;
                    } else if gull.y > GULL_Y_MAX {
                        gull.y = GULL_Y_MAX;
                        *vy = -*vy;
                    }
                }
            }
        }
        for slot in &mut self.rings {
            if let Some(ring) = slot {
                ring.x -= scroll_step;
                if ring.x < px(-24) {
                    *slot = None;
                } else {
                    ring.y = self.sea.at(self.scroll + ring.x as i32) - RING_LIFT;
                }
            }
        }
        if let Some(heart) = self.heart_pickup.as_mut() {
            heart.x -= scroll_step;
            if heart.x < px(-24) {
                self.heart_pickup = None;
            } else {
                heart.y = self.sea.at(self.scroll + heart.x as i32) - RING_LIFT;
            }
        }

        let tier = tier(self.tick);
        self.gull_timer = self.gull_timer.saturating_sub(1);
        if self.gull_timer == 0 {
            self.gull_timer = gull_interval(tier);
            let roll = self.roll();
            let (lane, glide, turn) = gull_flight(roll);
            self.spawn_gull(px(SCREEN_W + 16), lane, glide, turn);
            if tier >= GULL_PAIR_TIER {
                // The partner mirrors the leader: other side of the band, opposite glide.
                let lane = GULL_LANES as i16 - 1 - lane;
                self.spawn_gull(px(SCREEN_W + 16) + GULL_PAIR_GAP, lane, -glide, turn);
            }
        }

        self.ring_timer = self.ring_timer.saturating_sub(1);
        if self.ring_timer == 0 {
            self.ring_timer = ring_interval(tier);
            self.ring_count = self.ring_count.wrapping_add(1);
            let x = px(SCREEN_W + 24);
            spawn(
                &mut self.rings,
                Actor {
                    x,
                    y: self.sea.at(self.scroll + x as i32) - RING_LIFT,
                },
            );
            let hurt = self.hearts < START_HEARTS;
            if hurt && self.ring_count % HEART_EVERY_RINGS == 0 && self.heart_pickup.is_none() {
                let x = x + HEART_GAP;
                self.heart_pickup = Some(Actor {
                    x,
                    y: self.sea.at(self.scroll + x as i32) - RING_LIFT,
                });
            }
        }
    }

    fn spawn_gull(&mut self, x: i16, lane: i16, glide: i16, turn: u8) {
        if let Some(index) = self.gulls.iter().position(|slot| slot.is_none()) {
            self.gulls[index] = Some(Actor {
                x,
                y: GULL_Y_MIN + lane * 2 * FP,
            });
            self.gull_vy[index] = glide * GULL_GLIDE;
            self.gull_turn[index] = turn;
        }
    }

    fn touching_gull(&self) -> bool {
        let beaver = beaver_box(self.beaver);
        self.gulls
            .iter()
            .flatten()
            .any(|gull| overlap(beaver, gull_box(*gull)))
    }

    fn touching_heart(&self) -> bool {
        let Some(heart) = self.heart_pickup else {
            return false;
        };
        overlap(
            beaver_box(self.beaver),
            Rect {
                x: heart.x - px(8),
                y: heart.y - px(8),
                w: px(16),
                h: px(16),
            },
        )
    }

    /// Score only when the jumping beaver's body passes through the hoop.
    fn through_ring(&mut self) -> bool {
        if !self.beaver.airborne {
            return false;
        }
        let cx = self.beaver.x;
        let cy = self.beaver.y - px(14);
        let mut scored = false;
        for slot in &mut self.rings {
            if let Some(ring) = *slot {
                let inside = (cx - ring.x).abs() <= px(10) && (cy - ring.y).abs() <= px(10);
                if inside {
                    *slot = None;
                    scored = true;
                }
            }
        }
        scored
    }
}

impl Default for World {
    fn default() -> Self {
        Self::new()
    }
}

const RNG_SEED: u16 = 0xACE1;

impl World {
    /// Next pseudo-random value: xorshift16 after folding in the beaver's position.
    fn roll(&mut self) -> u16 {
        let mut x = self.rng ^ (self.beaver.x as u16);
        if x == 0 {
            x = RNG_SEED;
        }
        x ^= x << 7;
        x ^= x >> 9;
        x ^= x << 8;
        self.rng = x;
        x
    }
}

/// Lane, glide and zig-zag turn for one roll. One flight in four is straight, one zig-zags
/// across its lane, the rest glide diagonally; steep glides launch from the edge they fly
/// away from.
fn gull_flight(roll: u16) -> (i16, i16, u8) {
    let lane = (roll % GULL_LANES) as i16;
    let sign = if (roll >> 10) & 1 == 0 { 1 } else { -1 };
    match (roll >> 12) & 3 {
        0 => (lane, 0, 0),
        2 => (lane, sign * GULL_ZIG, GULL_ZIG_TICKS),
        _ => {
            let glide = sign * (GULL_STEEP + ((roll >> 8) % 3) as i16);
            let lane = if glide > 0 {
                lane % GULL_EDGE_LANES
            } else {
                GULL_LANES as i16 - 1 - lane % GULL_EDGE_LANES
            };
            (lane, glide, 0)
        }
    }
}

fn gull_interval(tier: u8) -> u16 {
    300u16.saturating_sub(tier as u16 * 16).max(160)
}

fn ring_interval(tier: u8) -> u16 {
    220u16.saturating_sub(tier as u16 * 10).max(120)
}

fn spawn<const N: usize>(slots: &mut [Option<Actor>; N], actor: Actor) {
    if let Some(slot) = slots.iter_mut().find(|slot| slot.is_none()) {
        *slot = Some(actor);
    }
}

fn beaver_box(beaver: Beaver) -> Rect {
    Rect {
        x: beaver.x - px(8),
        y: beaver.y - px(24),
        w: px(16),
        h: px(20),
    }
}

fn gull_box(actor: Actor) -> Rect {
    Rect {
        x: actor.x - px(10),
        y: actor.y - px(3),
        w: px(20),
        h: px(6),
    }
}

#[derive(Clone, Copy)]
struct Rect {
    x: i16,
    y: i16,
    w: i16,
    h: i16,
}

fn overlap(a: Rect, b: Rect) -> bool {
    a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y
}

#[cfg(test)]
mod tests {
    use super::*;

    fn run() -> World {
        let mut world = World::new();
        world.phase = Phase::Play;
        world.hearts = START_HEARTS;
        world.gull_timer = 10_000;
        world.ring_timer = 10_000;
        world.score = 0;
        world
    }

    /// Sets the world so that after the next step the beaver's feet sample `world_x` at `tick_after`.
    fn place(world: &mut World, world_x: i32, tick_after: u32) {
        world.beaver.x = BEAVER_X0;
        world.beaver.airborne = false;
        world.beaver.vx = 0;
        world.beaver.vy = 0;
        world.scroll = world_x - BEAVER_X0 as i32 - (world.speed >> 2) as i32;
        world.tick = tick_after - 1;
        world.sea.rebuild(tick_after, tier(tick_after));
        world.beaver.y = world.sea.at(world_x);
    }

    fn first_where(mut pred: impl FnMut(&Sea, i32) -> bool) -> (i32, u32) {
        for tick in 2..60u32 {
            let sea = Sea::new(tick, tier(tick));
            for x in 0..WAVE_PERIOD_PX {
                if pred(&sea, px(x) as i32) {
                    return (px(x) as i32, tick);
                }
            }
        }
        panic!("no matching wave sample");
    }

    fn jump_at(world_x: i32, tick: u32, speed: i16, right: bool) -> World {
        let mut world = run();
        world.speed = speed;
        place(&mut world, world_x, tick);
        let events = world.step(Input {
            right,
            jump: true,
            ..Input::default()
        });
        assert!(events.jumped);
        assert!(world.beaver.airborne);
        world
    }

    fn rising_face() -> (i32, u32) {
        first_where(|sea, x| {
            !sea.is_crest(x) && !sea.is_trough(x) && !sea.in_top_fifth(x) && sea.slope(x) < 0
        })
    }

    #[test]
    fn jump_is_accepted_on_a_rising_face_and_on_a_crest() {
        let (rise_x, rise_tick) = rising_face();
        let (crest_x, crest_tick) = first_where(|sea, x| sea.is_crest(x));
        let early = jump_at(rise_x, rise_tick, SCROLL_SPEED, false);
        let crest = jump_at(crest_x, crest_tick, SCROLL_SPEED, false);

        assert!(early.beaver.vy > crest.beaver.vy);
        assert!(early.speed < SCROLL_SPEED);
        assert!(crest.speed > SCROLL_SPEED);
    }

    #[test]
    fn right_and_carried_speed_compound_into_higher_jumps() {
        let (crest_x, crest_tick) = first_where(|sea, x| sea.is_crest(x));
        let plain = jump_at(crest_x, crest_tick, SCROLL_SPEED, false);
        let tug = jump_at(crest_x, crest_tick, SCROLL_SPEED, true);
        let fast = jump_at(crest_x, crest_tick, 2 * VEL, true);
        assert!(tug.beaver.vy < plain.beaver.vy);
        assert!(fast.beaver.vy < tug.beaver.vy);

        let (down_x, down_tick) = first_where(|sea, x| sea.slope(x) > px(2));
        let mut coast = run();
        place(&mut coast, down_x, down_tick);
        coast.step(Input::default());
        let mut pump = run();
        place(&mut pump, down_x, down_tick);
        pump.step(Input {
            right: true,
            ..Input::default()
        });
        assert!(coast.speed > SCROLL_SPEED);
        assert!(pump.speed > coast.speed);

        let (up_x, up_tick) = first_where(|sea, x| sea.slope(x) < -px(2));
        let mut climb = run();
        place(&mut climb, up_x, up_tick);
        climb.step(Input::default());
        assert!(climb.speed < SCROLL_SPEED);
    }

    #[test]
    fn jump_is_refused_in_a_trough_on_the_way_down_and_in_the_air() {
        let (trough_x, trough_tick) = first_where(|sea, x| sea.is_trough(x));
        let mut trough = run();
        place(&mut trough, trough_x, trough_tick);
        let events = trough.step(Input {
            jump: true,
            ..Input::default()
        });
        assert!(!events.jumped);
        assert!(!trough.beaver.airborne);
        assert_eq!(trough.beaver.y, trough.sea.at(trough.beaver_world_x()));

        let (down_x, down_tick) =
            first_where(|sea, x| !sea.is_crest(x) && !sea.is_trough(x) && sea.slope(x) > 0);
        let mut down = run();
        place(&mut down, down_x, down_tick);
        let events = down.step(Input {
            jump: true,
            ..Input::default()
        });
        assert!(!events.jumped);
        assert!(!down.beaver.airborne);

        let mut air = run();
        air.tick = 10;
        air.beaver.airborne = true;
        air.beaver.vy = 0;
        air.beaver.y = px(30);
        let events = air.step(Input {
            jump: true,
            ..Input::default()
        });
        assert!(!events.jumped);
        assert!(air.beaver.airborne);
        assert_eq!(air.beaver.vy, GRAVITY);
    }

    #[test]
    fn landing_returns_the_beaver_to_the_surface() {
        let mut world = run();
        world.tick = 8;
        world.beaver.airborne = true;
        world.beaver.vy = 20 * VEL;
        world.beaver.y = px(150);
        let events = world.step(Input::default());
        assert!(events.landed);
        assert!(!world.beaver.airborne);
        assert_eq!(world.beaver.vy, 0);
        assert_eq!(world.beaver.y, world.sea.at(world.beaver_world_x()));
        assert!(world.splash.is_some());
    }

    fn land_at(world_x: i32, tick: u32, combo: u8) -> (World, Events) {
        let mut world = run();
        world.combo = combo;
        place(&mut world, world_x, tick);
        world.beaver.airborne = true;
        world.beaver.vy = 6 * VEL;
        world.beaver.y -= px(1);
        let events = world.step(Input::default());
        assert!(events.landed);
        (world, events)
    }

    #[test]
    fn a_downhill_landing_boosts_and_chains_while_an_uphill_landing_stalls() {
        let (down_x, down_tick) = first_where(|sea, x| sea.slope(x) > px(2));
        let (up_x, up_tick) = first_where(|sea, x| sea.slope(x) < -px(2));

        let (clean, events) = land_at(down_x, down_tick, 0);
        assert!(events.clean_landing);
        assert_eq!(clean.combo, 1);
        assert!(clean.speed > SCROLL_SPEED);
        assert!(clean.splash.is_some_and(|splash| splash.big));

        let (chained, _) = land_at(down_x, down_tick, 2);
        assert_eq!(chained.combo, 3);
        assert!(chained.speed > clean.speed);

        let (stall, events) = land_at(up_x, up_tick, 2);
        assert!(!events.clean_landing);
        assert_eq!(stall.combo, 0);
        assert!(stall.speed < SCROLL_SPEED);
        assert!(stall.splash.is_some_and(|splash| !splash.big));
    }

    #[test]
    fn the_jump_meter_is_green_exactly_when_a_jump_gets_the_top_fifth_bonus() {
        let (green_start, green_end) = METER_GREEN;
        let mut seen = [false; 3];
        for tick in 2..40u32 {
            let sea = Sea::new(tick, tier(tick));
            for x in 0..WAVE_PERIOD_PX {
                let world_x = px(x) as i32;
                let meter = sea.jump_meter(world_x);
                assert!((0..=METER_LEN as i8).contains(&meter));
                let green = (green_start..green_end).contains(&meter);
                let bonus = sea.jump_allowed(world_x) && sea.in_top_fifth(world_x);
                assert_eq!(green, bonus, "x={x} tick={tick}");
                let zone = if meter < green_start {
                    0
                } else if green {
                    1
                } else {
                    2
                };
                seen[zone] = true;
                if zone == 2 {
                    assert!(!sea.jump_allowed(world_x));
                }
            }
        }
        assert_eq!(seen, [true; 3]);

        let (crest_x, crest_tick) = first_where(|sea, x| sea.is_crest(x));
        let mut world = run();
        place(&mut world, crest_x, crest_tick);
        world.step(Input::default());
        assert!(world.meter >= 0);
        let at_take_off = jump_at(crest_x, crest_tick, SCROLL_SPEED, false).meter;
        assert!((green_start..green_end).contains(&at_take_off));
    }

    #[test]
    fn the_arrow_glides_toward_its_target_a_few_pixels_per_frame() {
        let mut world = run();
        world.tick = 10;
        world.meter = 0;
        let mut last = 0;
        for _ in 0..200 {
            world.step(Input::default());
            let shown = world.meter;
            assert!((0..=METER_LEN as i8).contains(&shown));
            assert!((shown - last).abs() <= METER_RATE);
            last = shown;
        }
    }

    #[test]
    fn a_completed_backflip_pays_off_and_a_half_flip_wipes_out() {
        let (crest_x, crest_tick) = first_where(|sea, x| sea.is_crest(x));
        let release = Input::default();
        let press = Input {
            jump: true,
            ..Input::default()
        };

        // Holding jump through take-off does not start a flip; a fresh press does.
        let mut flip = jump_at(crest_x, crest_tick, SCROLL_SPEED, false);
        flip.step(press);
        assert_eq!(flip.beaver.flip, 0);
        flip.step(release);
        flip.step(press);
        assert_eq!(flip.beaver.flip, FLIP_FRAMES - 1);
        let mut frames = 0;
        let mut events = Events::default();
        while flip.beaver.airborne {
            events = flip.step(release);
            frames += 1;
            assert!(frames < 200);
        }
        assert_eq!(flip.beaver.flips, 0);
        assert!(events.trick);
        assert!(!events.wipeout);
        assert!(matches!(flip.banner, Banner::Trick { mult: 2, .. }));
        assert!(flip.banner_timer > 0);

        let mut plain = jump_at(crest_x, crest_tick, SCROLL_SPEED, false);
        while plain.beaver.airborne {
            plain.step(release);
        }
        assert!(flip.score > plain.score);
        assert!(flip.speed > plain.speed);

        // Pressing again near the water lands mid-flip.
        let mut late = jump_at(crest_x, crest_tick, SCROLL_SPEED, false);
        late.combo = 3;
        while late.beaver.vy < 2 * VEL {
            late.step(release);
        }
        late.step(press);
        assert!(late.beaver.flip > 0);
        let mut events = Events::default();
        while late.beaver.airborne {
            events = late.step(release);
        }
        assert!(events.wipeout);
        assert!(!events.trick);
        assert_eq!(late.speed, MIN_SPEED);
        assert_eq!(late.combo, 0);
        assert_eq!(late.banner, Banner::Wipeout);
        assert_eq!(late.hearts, START_HEARTS - 1);
        assert!(late.invuln > WIPEOUT_FRAMES);
        assert_eq!(late.stun, WIPEOUT_FRAMES);
        let x_before = late.beaver.x;
        late.step(Input {
            right: true,
            jump: true,
            ..Input::default()
        });
        assert_eq!(late.beaver.x, x_before);
        assert!(!late.beaver.airborne);
    }

    #[test]
    fn a_wipeout_on_the_last_half_heart_ends_the_run() {
        let (crest_x, crest_tick) = first_where(|sea, x| sea.is_crest(x));
        let mut late = jump_at(crest_x, crest_tick, SCROLL_SPEED, false);
        late.hearts = 1;
        late.score = 40;
        while late.beaver.vy < 2 * VEL {
            late.step(Input::default());
        }
        late.step(Input {
            jump: true,
            ..Input::default()
        });
        let mut events = Events::default();
        while late.beaver.airborne {
            events = late.step(Input::default());
        }
        assert!(events.wipeout);
        assert!(events.died);
        assert_eq!(late.phase, Phase::Over);
        assert_eq!(late.best, late.score);
        assert!(late.best >= 40);
    }

    #[test]
    fn a_perfect_jump_banks_air_time_and_rings_multiply_it() {
        let (crest_x, crest_tick) = first_where(|sea, x| sea.is_crest(x));
        let release = Input::default();

        let mut perfect = jump_at(crest_x, crest_tick, SCROLL_SPEED, false);
        assert!(perfect.beaver.perfect);
        let before = perfect.score;
        let mut events = Events::default();
        while perfect.beaver.airborne {
            events = perfect.step(release);
        }
        assert!(events.trick);
        let Banner::Trick {
            points,
            mult: 1,
            air: true,
        } = perfect.banner
        else {
            panic!("expected an air-time banner, got {:?}", perfect.banner);
        };
        assert!(points >= 10);
        assert!(perfect.score >= before + points as u32);

        let (rise_x, rise_tick) = rising_face();
        let mut early = jump_at(rise_x, rise_tick, SCROLL_SPEED, false);
        assert!(!early.beaver.perfect);
        let mut events = Events::default();
        while early.beaver.airborne {
            events = early.step(release);
        }
        assert!(!events.trick);

        let mut ringed = run();
        arm_ring(&mut ringed, crest_x, crest_tick);
        ringed.step(Input {
            jump: true,
            ..Input::default()
        });
        let mut caught = false;
        while ringed.beaver.airborne {
            caught |= ringed.step(release).ring;
        }
        assert!(caught);
        assert!(matches!(
            ringed.banner,
            Banner::Trick {
                mult: 2,
                air: true,
                ..
            }
        ));
    }

    #[test]
    fn a_heart_rides_beside_every_third_ring_while_hurt_and_restores_half_a_heart() {
        let mut world = run();
        world.hearts = START_HEARTS - 2;
        let mut hearts_seen = 0;
        for _ in 0..3 {
            world.ring_timer = 1;
            world.step(Input::default());
            if world.heart_pickup.is_some() {
                hearts_seen += 1;
                world.heart_pickup = None;
            }
        }
        assert_eq!(hearts_seen, 1);

        let mut full = run();
        full.ring_timer = 1;
        full.ring_count = HEART_EVERY_RINGS - 1;
        full.step(Input::default());
        assert!(full.heart_pickup.is_none());

        // Pickups ride at ring height, so the beaver meets one in the air.
        let mut world = run();
        world.hearts = START_HEARTS - 1;
        world.tick = 10;
        world.beaver.airborne = true;
        world.beaver.y = world.sea.at(world.beaver_world_x()) - px(30);
        world.heart_pickup = Some(Actor {
            x: world.beaver.x + (world.speed >> 2),
            y: 0,
        });
        let events = world.step(Input::default());
        assert!(events.heart);
        assert_eq!(world.hearts, START_HEARTS);
        assert!(world.heart_pickup.is_none());
        assert_eq!(world.banner, Banner::Heart);
    }

    #[test]
    fn hugging_a_downhill_face_with_down_pumps_like_right_and_flow_doubles_time_score() {
        let (down_x, down_tick) = first_where(|sea, x| sea.slope(x) > px(2));
        let mut coast = run();
        place(&mut coast, down_x, down_tick);
        coast.step(Input::default());
        let mut hug = run();
        place(&mut hug, down_x, down_tick);
        hug.step(Input {
            dive: true,
            ..Input::default()
        });
        assert!(hug.speed > coast.speed);

        let mut flow = run();
        flow.tick = 3;
        flow.combo = FLOW_COMBO;
        flow.combo_timer = 1000;
        flow.step(Input::default());
        assert_eq!(flow.score, 2);
        let mut calm = run();
        calm.tick = 3;
        calm.step(Input::default());
        assert_eq!(calm.score, 1);
    }

    #[test]
    fn swimming_left_is_as_quick_as_right_and_reverses_fast() {
        let mut world = run();
        world.tick = 10;
        for _ in 0..20 {
            world.step(Input {
                right: true,
                ..Input::default()
            });
        }
        let after_right = world.beaver.x;
        assert!(world.beaver.vx > 0);
        let mut frames = 0;
        while world.beaver.vx >= 0 {
            world.step(Input {
                left: true,
                ..Input::default()
            });
            frames += 1;
        }
        assert!(frames <= 8);
        for _ in 0..40 {
            world.step(Input {
                left: true,
                ..Input::default()
            });
        }
        assert!(world.beaver.x < after_right - px(40));
    }

    #[test]
    fn a_steep_face_steers_less_and_the_beaver_rides_the_surface() {
        let tick = 10;
        let sea = Sea::new(tick, 0);
        let mut flat_x = 0;
        let mut steep_x = 0;
        let mut flattest = i16::MAX;
        let mut steepest = -1;
        for x in 0..WAVE_PERIOD_PX {
            let steepness = sea.slope(px(x) as i32).abs();
            if steepness < flattest {
                flattest = steepness;
                flat_x = px(x) as i32;
            }
            if steepness > steepest {
                steepest = steepness;
                steep_x = px(x) as i32;
            }
        }
        assert!(steepest > flattest);
        assert!(steer_scale(flattest) > steer_scale(steepest));

        let mut flat = run();
        place(&mut flat, flat_x, tick);
        flat.step(Input {
            right: true,
            ..Input::default()
        });
        let mut steep = run();
        place(&mut steep, steep_x, tick);
        steep.step(Input {
            right: true,
            ..Input::default()
        });
        let flat_dx = flat.beaver.x - BEAVER_X0;
        let steep_dx = steep.beaver.x - BEAVER_X0;
        assert!(flat_dx > steep_dx);
        assert!(steep_dx * 2 >= flat_dx);

        let mut world = run();
        world.tick = 20;
        for _ in 0..30 {
            world.step(Input::default());
            assert!(!world.beaver.airborne);
            assert_eq!(world.beaver.y, world.sea.at(world.beaver_world_x()));
        }
    }

    fn arm_gull(world: &mut World) {
        let tick = world.tick + 1;
        let x = world.beaver.x;
        let step = world.speed >> 2;
        let sea = Sea::new(tick, tier(tick));
        let y = sea.at(world.scroll + step as i32 + x as i32);
        world.gulls[0] = Some(Actor {
            x: x + step + GULL_AIR_SPEED,
            y: y - px(12),
        });
        world.gull_timer = 10_000;
    }

    #[test]
    fn a_gull_removes_half_a_heart_and_invulnerability_blocks_the_next_hit() {
        let mut world = run();
        arm_gull(&mut world);
        let events = world.step(Input::default());
        assert!(events.hit);
        assert_eq!(world.hearts, START_HEARTS - 1);
        assert!(world.invuln > 0);

        arm_gull(&mut world);
        let events = world.step(Input::default());
        assert!(!events.hit);
        assert_eq!(world.hearts, START_HEARTS - 1);
    }

    #[test]
    fn the_sixth_hit_opens_game_over() {
        let mut world = run();
        world.score = 20;
        world.best = 5;
        for expected in (0..START_HEARTS).rev() {
            world.invuln = 0;
            world.phase = Phase::Play;
            arm_gull(&mut world);
            let events = world.step(Input::default());
            assert!(events.hit);
            assert_eq!(world.hearts, expected);
        }
        assert_eq!(world.phase, Phase::Over);
        assert!(!world.step(Input::default()).died);
        assert_eq!(world.best, world.score);
        assert!(world.best > 5);
    }

    fn arm_ring(world: &mut World, world_x: i32, tick_after: u32) {
        place(world, world_x, tick_after);
        world.rings[0] = Some(Actor {
            x: world.beaver.x + (world.speed >> 2),
            y: 0,
        });
        world.ring_timer = 10_000;
    }

    #[test]
    fn a_ring_scores_once() {
        let (crest_x, crest_tick) = first_where(|sea, x| sea.is_crest(x));
        let mut world = run();
        arm_ring(&mut world, crest_x, crest_tick);
        let mut scored = 0u32;
        let events = world.step(Input {
            jump: true,
            ..Input::default()
        });
        assert!(events.jumped);
        if events.ring {
            scored += 1;
        }
        for _ in 0..50 {
            if world.step(Input::default()).ring {
                scored += 1;
            }
        }
        assert_eq!(scored, 1);
        assert!(world.rings[0].is_none());
    }

    #[test]
    fn a_grounded_beaver_does_not_score_a_ring() {
        let (crest_x, crest_tick) = first_where(|sea, x| sea.is_crest(x));
        let mut world = run();
        arm_ring(&mut world, crest_x, crest_tick);
        for _ in 0..40 {
            let events = world.step(Input::default());
            assert!(!events.ring);
            assert!(!world.beaver.airborne);
        }
        assert!(world.rings[0].is_some());
    }

    #[test]
    fn score_advances_with_time_and_a_best_is_kept_only_when_beaten() {
        let mut world = World::new();
        world.best = 9;
        world.step(Input {
            start: true,
            ..Input::default()
        });
        assert_eq!(world.phase, Phase::Play);
        for _ in 0..4 {
            world.step(Input::default());
        }
        assert_eq!(world.score, 1);

        world.hearts = 1;
        world.score = 4;
        world.invuln = 0;
        arm_gull(&mut world);
        let events = world.step(Input::default());
        assert!(events.died);
        assert_eq!(world.best, 9);

        world.phase = Phase::Play;
        world.hearts = 1;
        world.score = 30;
        world.invuln = 0;
        arm_gull(&mut world);
        world.step(Input::default());
        assert_eq!(world.phase, Phase::Over);
        assert_eq!(world.best, 30);

        world.step(Input::default());
        world.step(Input {
            start: true,
            ..Input::default()
        });
        assert_eq!(world.phase, Phase::Title);
        assert_eq!(world.best, 30);
    }

    #[test]
    fn a_scripted_run_stays_in_the_lane_and_scores_only_time() {
        let mut world = World::new();
        world.best = 7;
        world.step(Input {
            start: true,
            ..Input::default()
        });
        for _ in 0..240 {
            world.step(Input::default());
            assert!((LANE_MIN..=LANE_MAX).contains(&world.beaver.x));
            assert!((MIN_SPEED..=MAX_SPEED).contains(&world.speed));
            assert_eq!(world.phase, Phase::Play);
            assert_eq!(world.hearts, START_HEARTS);
        }
        assert_eq!(world.score, 60);
        assert_eq!(world.best, 7);
    }

    #[test]
    fn input_bits_round_trip() {
        for bits in 0..32u8 {
            assert_eq!(Input::from_bits(bits).to_bits(), bits);
        }
    }
}
