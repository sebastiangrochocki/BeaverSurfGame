//! Background music on the GBA's four legacy Game Boy channels, which the sample mixer leaves
//! free: pulse 1 melody, pulse 2 off-beat plucks, the wave channel on bass and noise on a light
//! kit. Same sequencer and the same tune as the Game Boy Color port (see `song.rs`).

use crate::song;

const SOUND1CNT_L: *mut u16 = 0x0400_0060 as *mut u16;
const SOUND1CNT_H: *mut u16 = 0x0400_0062 as *mut u16;
const SOUND1CNT_X: *mut u16 = 0x0400_0064 as *mut u16;
const SOUND2CNT_L: *mut u16 = 0x0400_0068 as *mut u16;
const SOUND2CNT_H: *mut u16 = 0x0400_006C as *mut u16;
const SOUND3CNT_L: *mut u16 = 0x0400_0070 as *mut u16;
const SOUND3CNT_H: *mut u16 = 0x0400_0072 as *mut u16;
const SOUND3CNT_X: *mut u16 = 0x0400_0074 as *mut u16;
const SOUND4CNT_L: *mut u16 = 0x0400_0078 as *mut u16;
const SOUND4CNT_H: *mut u16 = 0x0400_007C as *mut u16;
const SOUNDCNT_L: *mut u16 = 0x0400_0080 as *mut u16;
const SOUNDCNT_H: *mut u16 = 0x0400_0082 as *mut u16;
const WAVE_RAM: *mut u16 = 0x0400_0090 as *mut u16;

fn put(reg: *mut u16, value: u16) {
    // SAFETY: fixed memory-mapped sound registers; writes have no other side effects.
    unsafe { reg.write_volatile(value) };
}

fn get(reg: *mut u16) -> u16 {
    // SAFETY: see `put`.
    unsafe { reg.read_volatile() }
}

const TRACKS: [&[(u8, u8)]; 4] = [song::MELODY, song::HARMONY, song::BASS, song::DRUMS];

pub struct Music {
    pos: [usize; 4],
    left: [u8; 4],
    step_frames: u8,
}

impl Music {
    /// Call after the mixer is enabled: it owns SOUNDCNT_H, and this only raises the legacy
    /// channels' share of the output to full.
    pub fn start() -> Self {
        put(SOUNDCNT_L, 0xFF77);
        put(SOUNDCNT_H, get(SOUNDCNT_H) | 0x0002);
        // Wave RAM writes land in the bank that is not selected: point at bank 1, fill bank 0.
        put(SOUND3CNT_L, 0x0040);
        for (i, pair) in song::WAVE.chunks(2).enumerate() {
            // SAFETY: WAVE_RAM is sixteen bytes of registers; i < 8.
            unsafe { WAVE_RAM.add(i).write_volatile(pair[0] as u16 | ((pair[1] as u16) << 8)) };
        }
        put(SOUND3CNT_L, 0x0080);
        Music {
            pos: [0; 4],
            left: [0; 4],
            step_frames: 1,
        }
    }

    /// Advance every track by one frame.
    pub fn tick(&mut self) {
        self.step_frames -= 1;
        if self.step_frames != 0 {
            return;
        }
        self.step_frames = song::STEP_FRAMES;
        for ch in 0..4 {
            if self.left[ch] > 0 {
                self.left[ch] -= 1;
                if self.left[ch] > 0 {
                    continue;
                }
            }
            let track = TRACKS[ch];
            if self.pos[ch] >= track.len() {
                self.pos[ch] = 0;
            }
            let (note, steps) = track[self.pos[ch]];
            self.left[ch] = steps;
            self.pos[ch] += 1;
            play(ch, note);
        }
    }
}

fn freq(note: u8) -> u16 {
    song::FREQ[(note - song::LOW_NOTE) as usize]
}

fn play(ch: usize, note: u8) {
    match ch {
        0 => {
            put(SOUND1CNT_L, 0x0000);
            if note == 0 {
                put(SOUND1CNT_H, 0x0800);
                put(SOUND1CNT_X, 0x8000);
            } else {
                put(SOUND1CNT_H, 0x9380);
                put(SOUND1CNT_X, 0x8000 | freq(note));
            }
        }
        1 => {
            if note != 0 {
                put(SOUND2CNT_L, 0x6140);
                put(SOUND2CNT_H, 0x8000 | freq(note));
            }
        }
        2 => {
            if note == 0 {
                put(SOUND3CNT_H, 0x0000);
            } else {
                put(SOUND3CNT_H, 0x4000);
                put(SOUND3CNT_X, 0x8000 | freq(note + 12));
            }
        }
        _ => {
            let (shape, envelope): (u16, u16) = match note {
                1 => (0x61, 0xA1),
                2 => (0x43, 0x81),
                3 => (0x01, 0x41),
                _ => return,
            };
            put(SOUND4CNT_L, (envelope << 8) | 0x3F);
            put(SOUND4CNT_H, 0x8000 | shape);
        }
    }
}
