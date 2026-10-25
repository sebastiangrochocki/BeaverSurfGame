/* Register-level sound. The Game Boy has no sample playback, so each cue is a pulse or a noise
 * burst, and the music is a four-track sequencer: pulse 1 melody, pulse 2 off-beat plucks, the
 * wave channel on bass and the noise channel on a light kit. A cue borrows its channel for a
 * few frames; the sequencer keeps counting and picks the channel back up at the next note. */
#include "sound.h"

#include <gb/gb.h>

#include "song.h"

enum { CH_MELODY, CH_HARMONY, CH_BASS, CH_DRUMS, CH_COUNT };

static const uint8_t *const tracks[CH_COUNT] = {SONG_MELODY, SONG_HARMONY, SONG_BASS, SONG_DRUMS};
static const uint8_t track_len[CH_COUNT] = {
    sizeof(SONG_MELODY), sizeof(SONG_HARMONY), sizeof(SONG_BASS), sizeof(SONG_DRUMS)};

/* Byte offset of the next (note, steps) pair, steps left on the current one, and frames a cue
 * still owns the channel. */
static uint8_t pos[CH_COUNT];
static uint8_t left[CH_COUNT];
static uint8_t hold[CH_COUNT];
static uint8_t step_frames;

#define HOLD_JUMP 18
#define HOLD_PING 14
#define HOLD_NOISE 16

static void load_wave(void) {
    uint8_t i;
    NR30_REG = 0x00;
    for (i = 0; i < 16; i++) ((volatile uint8_t *)0xFF30)[i] = SONG_WAVE[i];
    NR30_REG = 0x80;
}

void sound_init(void) {
    uint8_t ch;
    NR52_REG = 0x80;
    NR50_REG = 0x77;
    NR51_REG = 0xFF;
    load_wave();
    for (ch = 0; ch < CH_COUNT; ch++) {
        pos[ch] = 0;
        left[ch] = 0;
        hold[ch] = 0;
    }
    step_frames = 1;
}

static void pulse_up(void) {
    NR10_REG = 0x15;
    NR11_REG = 0x80;
    NR12_REG = 0xF2;
    NR13_REG = 0x40;
    NR14_REG = 0x85;
}

static void pulse_high(void) {
    NR21_REG = 0x80;
    NR22_REG = 0xF3;
    NR23_REG = 0xC0;
    NR24_REG = 0x87;
}

static void noise(uint8_t shape, uint8_t envelope) {
    NR41_REG = 0x3F;
    NR42_REG = envelope;
    NR43_REG = shape;
    NR44_REG = 0xC0;
}

void sound_events(const Events *events) {
    if (events->wipeout || events->hit) {
        noise(0x6B, 0xF1);
        hold[CH_DRUMS] = HOLD_NOISE;
    } else if (events->landed) {
        noise(0x3A, 0xA2);
        hold[CH_DRUMS] = HOLD_NOISE;
    }
    if (events->jumped) {
        pulse_up();
        hold[CH_MELODY] = HOLD_JUMP;
    }
    if (events->ring || events->heart || events->trick) {
        pulse_high();
        hold[CH_HARMONY] = HOLD_PING;
    }
}

/* Breathy sustained lead, like a dizi: 50% duty, slow decay. */
static void melody_note(uint8_t note) {
    uint16_t f;
    NR10_REG = 0x00;
    if (note == 0) {
        NR12_REG = 0x08;
        NR14_REG = 0x80;
        return;
    }
    f = SONG_FREQ[note - SONG_LOW_NOTE];
    NR11_REG = 0x80;
    NR12_REG = 0x93;
    NR13_REG = (uint8_t)f;
    NR14_REG = (uint8_t)(0x80 | (f >> 8));
}

/* Short plucked chord tone on the off-beats, like a yueqin: 25% duty, quick decay. */
static void harmony_note(uint8_t note) {
    uint16_t f;
    if (note == 0) return;
    f = SONG_FREQ[note - SONG_LOW_NOTE];
    NR21_REG = 0x40;
    NR22_REG = 0x61;
    NR23_REG = (uint8_t)f;
    NR24_REG = (uint8_t)(0x80 | (f >> 8));
}

/* Triangle bass. The wave channel sounds an octave below the pulse register value. */
static void bass_note(uint8_t note) {
    uint16_t f;
    if (note == 0) {
        NR32_REG = 0x00;
        return;
    }
    f = SONG_FREQ[note + 12 - SONG_LOW_NOTE];
    NR31_REG = 0x00;
    NR32_REG = 0x40;
    NR33_REG = (uint8_t)f;
    NR34_REG = (uint8_t)(0x80 | (f >> 8));
}

static void drum_hit(uint8_t kind) {
    switch (kind) {
    case 1: noise(0x61, 0xA1); break; /* kick: low, short */
    case 2: noise(0x43, 0x81); break; /* snare: mid, short */
    case 3: noise(0x01, 0x41); break; /* tick: high, very short */
    default: break;
    }
}

static void play(uint8_t ch, uint8_t note) {
    switch (ch) {
    case CH_MELODY: melody_note(note); break;
    case CH_HARMONY: harmony_note(note); break;
    case CH_BASS: bass_note(note); break;
    default: drum_hit(note); break;
    }
}

/* Cuts the sustained voices when the game pauses; the next note-on brings each one back. */
void sound_pause(void) {
    melody_note(0);
    NR22_REG = 0x08;
    NR24_REG = 0x80;
    bass_note(0);
}

/* Once per rules step. Advances every track by one frame; a channel a cue is using skips its
 * note-on but still counts, so the band stays in time. */
void sound_tick(void) {
    uint8_t ch;
    for (ch = 0; ch < CH_COUNT; ch++) {
        if (hold[ch]) hold[ch]--;
    }
    if (--step_frames) return;
    step_frames = SONG_STEP_FRAMES;
    for (ch = 0; ch < CH_COUNT; ch++) {
        const uint8_t *track;
        uint8_t note;
        if (left[ch]) {
            left[ch]--;
            if (left[ch]) continue;
        }
        track = tracks[ch];
        if (pos[ch] >= track_len[ch]) pos[ch] = 0;
        note = track[pos[ch]];
        left[ch] = track[pos[ch] + 1];
        pos[ch] += 2;
        if (!hold[ch]) play(ch, note);
    }
}
