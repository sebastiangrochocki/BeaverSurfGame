//! Fixed-layout byte encoding of the whole rules state.
//!
//! The golden-trace fixtures store one record per frame, and `gbc/src/rules.c` encodes its own
//! state the same way so the host test can compare the two byte for byte.
//! Every multi-byte field is little-endian.

use crate::{Actor, Banner, Phase, Splash, World};

/// Size of one encoded state record.
pub const STATE_LEN: usize = 126;

/// Trace file: `BVRT`, version, state length, two reserved bytes, frame count (u32),
/// then one record per frame: the input bits fed to `step`, followed by the state after it.
/// Record 0 holds the fresh world with a zero input byte.
pub const TRACE_MAGIC: [u8; 4] = *b"BVRT";
pub const TRACE_VERSION: u8 = 1;
pub const TRACE_HEADER_LEN: usize = 12;
pub const RECORD_LEN: usize = 1 + STATE_LEN;

pub fn trace_header(frames: u32) -> [u8; TRACE_HEADER_LEN] {
    let mut header = [0u8; TRACE_HEADER_LEN];
    header[0..4].copy_from_slice(&TRACE_MAGIC);
    header[4] = TRACE_VERSION;
    header[5] = STATE_LEN as u8;
    header[8..12].copy_from_slice(&frames.to_le_bytes());
    header
}

/// Number of records in a trace file, or `None` if the header is not ours.
pub fn trace_frames(bytes: &[u8]) -> Option<usize> {
    if bytes.len() < TRACE_HEADER_LEN
        || bytes[0..4] != TRACE_MAGIC
        || bytes[4] != TRACE_VERSION
        || bytes[5] as usize != STATE_LEN
    {
        return None;
    }
    let frames = u32::from_le_bytes([bytes[8], bytes[9], bytes[10], bytes[11]]) as usize;
    (bytes.len() == TRACE_HEADER_LEN + frames * RECORD_LEN).then_some(frames)
}

/// Input bits and encoded state of record `index`.
pub fn trace_record(bytes: &[u8], index: usize) -> (u8, &[u8]) {
    let at = TRACE_HEADER_LEN + index * RECORD_LEN;
    (bytes[at], &bytes[at + 1..at + RECORD_LEN])
}

struct Writer<'a> {
    out: &'a mut [u8],
    at: usize,
}

impl Writer<'_> {
    fn u8(&mut self, value: u8) {
        self.out[self.at] = value;
        self.at += 1;
    }

    fn i8(&mut self, value: i8) {
        self.u8(value as u8);
    }

    fn bool(&mut self, value: bool) {
        self.u8(value as u8);
    }

    fn u16(&mut self, value: u16) {
        self.out[self.at..self.at + 2].copy_from_slice(&value.to_le_bytes());
        self.at += 2;
    }

    fn i16(&mut self, value: i16) {
        self.u16(value as u16);
    }

    fn u32(&mut self, value: u32) {
        self.out[self.at..self.at + 4].copy_from_slice(&value.to_le_bytes());
        self.at += 4;
    }

    fn i32(&mut self, value: i32) {
        self.u32(value as u32);
    }

    fn actor(&mut self, actor: Option<Actor>) {
        match actor {
            Some(actor) => {
                self.u8(1);
                self.i16(actor.x);
                self.i16(actor.y);
            }
            None => {
                self.u8(0);
                self.i16(0);
                self.i16(0);
            }
        }
    }

    fn splash(&mut self, splash: Option<Splash>) {
        match splash {
            Some(splash) => {
                self.u8(1);
                self.i16(splash.x);
                self.i16(splash.y);
                self.u8(splash.age);
                self.bool(splash.big);
            }
            None => {
                self.u8(0);
                self.i16(0);
                self.i16(0);
                self.u8(0);
                self.u8(0);
            }
        }
    }
}

impl World {
    /// Writes the full rules state into `out`.
    pub fn encode_state(&self, out: &mut [u8; STATE_LEN]) {
        let mut w = Writer { out, at: 0 };
        w.u32(self.tick);
        w.i32(self.scroll);
        w.i16(self.speed);

        let b = self.beaver;
        w.i16(b.x);
        w.i16(b.y);
        w.i16(b.vx);
        w.i16(b.vy);
        w.bool(b.airborne);
        w.u8(b.flip);
        w.u8(b.flips);
        w.bool(b.perfect);
        w.u16(b.air_time);
        w.u8(b.rings);
        w.u8(b.lane);
        w.u8(b.lane_to);
        w.bool(b.cleared);

        for gull in self.gulls {
            w.actor(gull);
        }
        for vy in self.gull_vy {
            w.i16(vy);
        }
        for turn in self.gull_turn {
            w.u8(turn);
        }
        for ring in self.rings {
            w.actor(ring);
        }
        for lane in self.ring_lane {
            w.u8(lane);
        }
        w.actor(self.heart_pickup);
        w.u8(self.heart_lane);
        w.u16(self.ring_count);
        w.splash(self.splash);

        w.u8(self.combo);
        w.u16(self.combo_timer);
        w.i8(self.meter);
        w.u8(self.stun);

        let (kind, points, mult, air) = match self.banner {
            Banner::None => (0, 0, 0, false),
            Banner::Trick { points, mult, air } => (1, points, mult, air),
            Banner::Wipeout => (2, 0, 0, false),
            Banner::Heart => (3, 0, 0, false),
        };
        w.u8(kind);
        w.u16(points);
        w.u8(mult);
        w.bool(air);
        w.u8(self.banner_timer);
        w.u8(self.hearts);
        w.u32(self.score);
        w.u32(self.best);
        w.u8(match self.phase {
            Phase::Title => 0,
            Phase::Play => 1,
            Phase::Over => 2,
        });
        w.u8(self.invuln);
        w.u16(self.gull_timer);
        w.u16(self.ring_timer);
        w.bool(self.start_held);
        w.bool(self.jump_held);
        w.bool(self.dive_held);
        w.u8(self.down_tap);
        w.u8(self.up_tap);
        w.u16(self.rng);
        debug_assert_eq!(w.at, STATE_LEN);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::Input;

    #[test]
    fn the_record_fills_its_fixed_length_and_changes_with_the_world() {
        let mut world = World::new();
        let mut before = [0u8; STATE_LEN];
        world.encode_state(&mut before);
        world.step(Input {
            start: true,
            ..Input::default()
        });
        world.step(Input::default());
        let mut after = [0u8; STATE_LEN];
        world.encode_state(&mut after);
        assert_ne!(before, after);
        assert_eq!(
            u32::from_le_bytes(after[0..4].try_into().unwrap()),
            world.tick
        );
        assert_eq!(after[STATE_LEN - 3], 0);
    }

    #[test]
    fn the_header_round_trips_and_rejects_the_wrong_length() {
        let mut file = trace_header(2).to_vec();
        file.extend_from_slice(&[0u8; RECORD_LEN * 2]);
        assert_eq!(trace_frames(&file), Some(2));
        file.push(0);
        assert_eq!(trace_frames(&file), None);
    }
}
