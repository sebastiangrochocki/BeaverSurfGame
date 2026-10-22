//! Writes the golden traces in `fixtures/` that the C port must reproduce byte for byte.
//!
//! Each script is a deterministic player. Some look a few frames ahead with a copy of the
//! world to decide when a jump will reach a ring, a heart, or a gull; only the chosen inputs
//! end up in the file, so the C side just replays them.
//!
//! Run from the repository root: `cargo run --release --manifest-path sim/Cargo.toml --bin dump_traces`

use std::fs;
use std::path::{Path, PathBuf};

use beaver_sim::trace::{STATE_LEN, trace_header};
use beaver_sim::{Events, Input, Phase, Sea, VEL, World, encode_save, px, tier};

const TITLE_FRAMES: u32 = 20;
const OVER_FRAMES: u32 = 10;
const AFTER_FRAMES: u32 = 30;

#[derive(Clone, Copy, PartialEq, Eq)]
enum Style {
    Idle,
    Right,
    CrestJumper,
    Flipper,
    Wipeout,
    RingRun,
    HeartPickup,
    GullDeath,
}

struct Script {
    name: &'static str,
    style: Style,
    play_frames: u32,
}

const SCRIPTS: [Script; 8] = [
    Script {
        name: "idle",
        style: Style::Idle,
        play_frames: 2400,
    },
    Script {
        name: "hold_right",
        style: Style::Right,
        play_frames: 2400,
    },
    Script {
        name: "crest_jumper",
        style: Style::CrestJumper,
        play_frames: 2400,
    },
    Script {
        name: "flipper",
        style: Style::Flipper,
        play_frames: 2400,
    },
    Script {
        name: "wipeout",
        style: Style::Wipeout,
        play_frames: 3000,
    },
    Script {
        name: "ring_run",
        style: Style::RingRun,
        play_frames: 3600,
    },
    Script {
        name: "heart_pickup",
        style: Style::HeartPickup,
        play_frames: 4200,
    },
    Script {
        name: "gull_death",
        style: Style::GullDeath,
        play_frames: 9000,
    },
];

#[derive(Default)]
struct Tally {
    jumps: u32,
    clean: u32,
    tricks: u32,
    wipeouts: u32,
    rings: u32,
    hearts: u32,
    hits: u32,
    deaths: u32,
}

impl Tally {
    fn add(&mut self, events: Events) {
        self.jumps += events.jumped as u32;
        self.clean += events.clean_landing as u32;
        self.tricks += events.trick as u32;
        self.wipeouts += events.wipeout as u32;
        self.rings += events.ring as u32;
        self.hearts += events.heart as u32;
        self.hits += events.hit as u32;
        self.deaths += events.died as u32;
    }
}

/// Plays the world forward from `first` and then `hold` until the beaver lands or `frames` pass.
fn lookahead(world: &World, first: Input, hold: Input, dive_on_fall: bool, frames: u32) -> Events {
    let mut copy = *world;
    let mut seen = Events::default();
    let mut merge = |events: Events| {
        seen.ring |= events.ring;
        seen.heart |= events.heart;
        seen.hit |= events.hit;
        seen.died |= events.died;
        seen.clean_landing |= events.clean_landing;
        seen.landed |= events.landed;
        seen.wipeout |= events.wipeout;
    };
    merge(copy.step(first));
    for _ in 1..frames {
        if !copy.beaver.airborne || copy.phase != Phase::Play {
            break;
        }
        let dive = dive_on_fall && copy.beaver.vy > 0;
        merge(copy.step(Input { dive, ..hold }));
    }
    seen
}

struct Player {
    style: Style,
    /// Pending flip press for the current jump.
    flip_armed: bool,
    /// Hold pattern chosen by the planner for the current jump.
    plan: Option<(Input, bool)>,
    wiped_once: bool,
}

impl Player {
    fn new(style: Style) -> Self {
        Self {
            style,
            flip_armed: false,
            plan: None,
            wiped_once: false,
        }
    }

    fn jump_now(&mut self, world: &World, want: impl Fn(Events) -> bool) -> Option<Input> {
        let candidates = [
            (Input::default(), false),
            (
                Input {
                    right: true,
                    ..Input::default()
                },
                false,
            ),
            (Input::default(), true),
            (
                Input {
                    right: true,
                    ..Input::default()
                },
                true,
            ),
        ];
        for (hold, dive) in candidates {
            let first = Input { jump: true, ..hold };
            if want(lookahead(world, first, hold, dive, 160)) {
                self.plan = Some((hold, dive));
                return Some(first);
            }
        }
        None
    }

    fn input(&mut self, world: &World) -> Input {
        let right = Input {
            right: true,
            ..Input::default()
        };
        let next = Sea::new(world.tick + 1, tier(world.tick + 1));
        let ahead = world.beaver_world_x() + (world.speed >> 2) as i32;
        let grounded = !world.beaver.airborne;
        let green = grounded && next.in_top_fifth(ahead);
        match self.style {
            Style::Idle => Input::default(),
            Style::Right => right,
            Style::CrestJumper => Input {
                jump: green,
                ..right
            },
            Style::Flipper => {
                if grounded {
                    self.flip_armed = true;
                    return Input {
                        jump: green,
                        ..right
                    };
                }
                // One fresh press two frames after take-off, but only when the hop is long
                // enough for the flip to finish above the water.
                let press_input = Input {
                    jump: true,
                    ..right
                };
                let press = self.flip_armed
                    && world.beaver.air_time == 2
                    && !lookahead(world, press_input, right, false, 160).wipeout;
                if world.beaver.air_time >= 2 {
                    self.flip_armed = false;
                }
                if press { press_input } else { right }
            }
            Style::Wipeout => {
                if grounded {
                    self.flip_armed = true;
                    return Input {
                        jump: green,
                        ..right
                    };
                }
                let press = self.flip_armed && world.beaver.vy >= 2 * VEL;
                if press {
                    self.flip_armed = false;
                }
                Input {
                    jump: press,
                    ..right
                }
            }
            Style::RingRun | Style::HeartPickup | Style::GullDeath => {
                if !grounded {
                    if self.style == Style::HeartPickup && !self.wiped_once {
                        // Get hurt once so hearts start to appear.
                        if world.beaver.vy >= 2 * VEL {
                            self.wiped_once = true;
                            return Input {
                                jump: true,
                                ..Input::default()
                            };
                        }
                        return Input::default();
                    }
                    let (hold, dive) = self.plan.unwrap_or((Input::default(), false));
                    return Input {
                        dive: dive && world.beaver.vy > 0,
                        ..hold
                    };
                }
                self.plan = None;
                if world.stun > 0 || !next.jump_allowed(ahead) {
                    return Input::default();
                }
                if self.style == Style::HeartPickup && !self.wiped_once {
                    return Input {
                        jump: green,
                        ..Input::default()
                    };
                }
                let want: fn(Events) -> bool = match self.style {
                    Style::RingRun => |e| e.ring,
                    Style::HeartPickup => |e| e.heart || e.ring,
                    _ => |e| e.hit || e.died,
                };
                if let Some(input) = self.jump_now(world, want) {
                    return input;
                }
                // Otherwise keep the chain going with clean landings from the green window.
                if green && self.style != Style::GullDeath {
                    if let Some(input) = self.jump_now(world, |e| e.clean_landing) {
                        return input;
                    }
                }
                Input::default()
            }
        }
    }
}

fn record(file: &mut Vec<u8>, input: Input, world: &World) {
    let mut state = [0u8; STATE_LEN];
    world.encode_state(&mut state);
    file.push(input.to_bits());
    file.extend_from_slice(&state);
}

fn run(script: &Script) -> (Vec<u8>, Tally, u32) {
    let mut world = World::new();
    let mut body = Vec::new();
    let mut tally = Tally::default();
    let mut frames = 0u32;
    record(&mut body, Input::default(), &world);
    frames += 1;
    let mut step = |world: &mut World, input: Input, body: &mut Vec<u8>, tally: &mut Tally| {
        let events = world.step(input);
        tally.add(events);
        record(body, input, world);
        frames += 1;
    };

    for frame in 0..TITLE_FRAMES {
        let start = frame == TITLE_FRAMES - 1;
        step(
            &mut world,
            Input {
                start,
                ..Input::default()
            },
            &mut body,
            &mut tally,
        );
    }
    assert_eq!(world.phase, Phase::Play);

    let mut player = Player::new(script.style);
    for _ in 0..script.play_frames {
        if world.phase != Phase::Play {
            break;
        }
        let input = player.input(&world);
        step(&mut world, input, &mut body, &mut tally);
    }

    if world.phase == Phase::Over {
        for frame in 0..OVER_FRAMES + AFTER_FRAMES {
            let start = frame == OVER_FRAMES;
            step(
                &mut world,
                Input {
                    start,
                    ..Input::default()
                },
                &mut body,
                &mut tally,
            );
        }
        assert_eq!(world.phase, Phase::Title);
    }

    let mut file = trace_header(frames).to_vec();
    file.extend_from_slice(&body);
    (file, tally, frames)
}

fn fixtures_dir() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../fixtures")
}

fn main() {
    let dir = fixtures_dir();
    fs::create_dir_all(&dir).expect("fixtures dir");
    let mut ok = true;
    for script in &SCRIPTS {
        let (file, tally, frames) = run(script);
        let path = dir.join(format!("{}.trace", script.name));
        fs::write(&path, &file).expect("write trace");
        let expected = match script.style {
            Style::Idle | Style::Right => tally.jumps == 0,
            Style::CrestJumper => tally.clean > 0 && tally.wipeouts == 0,
            Style::Flipper => tally.tricks > 0 && tally.wipeouts == 0,
            Style::Wipeout => tally.wipeouts > 0,
            Style::RingRun => tally.rings > 0,
            Style::HeartPickup => tally.hearts > 0,
            Style::GullDeath => tally.deaths == 1,
        };
        println!(
            "{:<14} {:>5} frames  jumps {:>3} clean {:>3} tricks {:>3} wipeouts {:>2} rings {:>3} hearts {:>2} hits {:>2} deaths {}{}",
            script.name,
            frames,
            tally.jumps,
            tally.clean,
            tally.tricks,
            tally.wipeouts,
            tally.rings,
            tally.hearts,
            tally.hits,
            tally.deaths,
            if expected {
                ""
            } else {
                "  <- missing its event"
            }
        );
        ok &= expected;
    }

    let best = 123_456u32;
    fs::write(dir.join("save_best_123456.bin"), encode_save(best)).expect("write save");
    let mut damaged = encode_save(best);
    damaged[7] ^= 0xFF;
    fs::write(dir.join("save_damaged.bin"), damaged).expect("write save");
    println!("save fixtures written for best {best}");

    let _ = px(0);
    if !ok {
        eprintln!("a script did not produce the event it exists for");
        std::process::exit(1);
    }
}
