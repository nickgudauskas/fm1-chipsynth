/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* chip_engine - polyphonic NES / Sega Genesis PSG style voice engine for the FM1 app layer.
 *
 * Integer-only audio path. It mixes into the block the sealed instrument engine
 * already rendered (signed Q23, interleaved L/R) and takes its notes from MIDI
 * messages on one channel (CHIP_MIDI_CHANNEL), so it never touches the track
 * sequencer or the binary engine.
 */
#ifndef CHIP_ENGINE_H
#define CHIP_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include "fm1.h"

/* 0-based MIDI channel the chip engine listens on (15 = channel 16). */
#define CHIP_MIDI_CHANNEL 15
/* Left shift from the engine's int16-scale sample to Q23. 8 = full scale. */
#define CHIP_OUT_SHIFT 7

#define CHIP_MAX_VOICES 8
#define CHIP_MACRO_LEN  32
#define CHIP_FRAME_HZ   60
#define CHIP_TBL_N      512
#define CHIP_ECHO_LEN   8192   /* echo buffer, samples (power of two): about 186 ms */
#define CHIP_LIMIT      24576  /* soft-limiter knee at int16 scale: above it, 1/4 slope */

typedef enum { CHIP_PULSE = 0, CHIP_TRI, CHIP_WAVE, CHIP_NOISE } chip_wave_t;
/* NES: 2A03 duties, triangle and nonlinear mixer. SN: Sega Genesis / Master
 * System SN76489 PSG, every tone voice a 50% square, 2 dB volume steps. */
typedef enum { CHIP_FLAVOR_NES = 0, CHIP_FLAVOR_SN } chip_flavor_t;
typedef enum { CHIP_MODE_POLY = 0, CHIP_MODE_MONO, CHIP_MODE_CHIP } chip_mode_t;

/* Per-frame sequence, stored as { len, loop, values... } (len 1..CHIP_MACRO_LEN;
 * loop 0xFF holds the last value). A null pointer disables the macro. The audio
 * path reads them, so in firmware they must be FM1_AUDIO_DATA like the preset.
 * CHIP_MACRO(name, loop, values...) defines one. */
typedef const int8_t *chip_macro_t;
#define CHIP_MACRO(name, loop_at, ...) \
    static const int8_t name[] FM1_AUDIO_DATA = { \
        (int8_t)sizeof((int8_t[]){ __VA_ARGS__ }), (int8_t)(loop_at), __VA_ARGS__ }

/* Names are for host tests and the demo only; the firmware never shows them. */
#ifdef FM1_HOST_TEST
#define CHIP_NAME(s) .name = s,
#else
#define CHIP_NAME(s)
#endif

struct chip_kit;

typedef struct {
#ifdef FM1_HOST_TEST
    const char *name;
#endif
    const uint8_t *wavetable; /* 32 4-bit samples for CHIP_WAVE (FM1_AUDIO_DATA) */
    const struct chip_kit *kit; /* drum kit: each key plays its own part (FM1_AUDIO_DATA) */
    uint8_t wave;            /* chip_wave_t */
    uint8_t duty;            /* 0..3 = 12.5 / 25 / 50 / 75 % */
    uint8_t noise_short;     /* 1 = short (metallic) LFSR */
    uint8_t decay_frames;    /* frames per volume step down to sustain; 0 = hold */
    uint8_t sustain;         /* 0..15 */
    uint8_t release_frames;  /* frames per step after note-off; 0 = cut */
    uint8_t vib_delay;       /* frames before vibrato starts */
    uint8_t vib_speed;       /* phase step per frame (256 = 1 cycle) */
    uint8_t vib_depth;       /* in 1/16 semitone */
    uint8_t macro_div;       /* frames per macro step (0 treated as 1) */
    chip_macro_t arp;        /* semitones */
    chip_macro_t duty_m;     /* 0..3 */
    chip_macro_t vol;        /* 0..15, scales envelope */
    chip_macro_t pitch;      /* 1/16 semitone */
} chip_inst_t;

/* Drum kit, by key within the octave (C = 0). A part plays at its own fixed
 * note whatever octave is pressed; a null part leaves that key silent. */
struct chip_kit {
    const chip_inst_t *part[12];
    uint8_t note[12];
};

typedef struct {
    uint8_t active, gate, type, vel, duty, env, env_cnt, sus, level, vib_ph;
    uint8_t mdiv;            /* frames since the last macro step */
    uint8_t mpos[4];         /* macro positions: arp, duty, vol, pitch */
    const chip_inst_t *inst;
    int note;                /* sounding note (a kit part's own note) */
    int key;                 /* key that started it, for note-off */
    uint32_t age;
    uint32_t phase, inc;
    uint16_t lfsr;
    uint32_t nphase, ninc;
    uint16_t frame;
    int32_t pitch_q8;        /* current pitch, Q8 semitones (portamento state) */
} chip_voice_t;

typedef struct {
    int sr;
    chip_flavor_t flavor;
    chip_mode_t mode;
    const chip_inst_t *inst;
    int num_voices;          /* poly pool size, <= CHIP_MAX_VOICES */
    int32_t bend_q8;         /* pitch bend, Q8 semitones */
    int32_t glide_q8;        /* mono portamento, Q8 semitones per frame; 0 = off */
    int gain_q8;             /* 256 = unity */
    chip_voice_t v[CHIP_MAX_VOICES];
    uint32_t age_ctr;
    int frame_acc;
    uint8_t held[8], held_vel[8];
    int nheld;
    int32_t x1, y1;          /* DC blocker */
    int32_t pulse_tbl[CHIP_TBL_N], tnd_tbl[CHIP_TBL_N];
    uint32_t inc_tbl[129];   /* phase increment per MIDI note (Q32 cycles/sample) */
    int echo_level;          /* Q8 wet level, 0 = off (CC 91) */
    int echo_fb;             /* Q8 feedback, at most about 0.78 (CC 86) */
    unsigned echo_delay;     /* samples, < CHIP_ECHO_LEN (CC 85) */
    unsigned echo_pos;
    int echo_tail;           /* glue: samples left to ring out after the last voice */
    int16_t echo_buf[CHIP_ECHO_LEN];
} chip_t;

/* ---- Core (usable standalone, e.g. in host tests) ---------------------- */
void chip_init(chip_t *c, int sample_rate);
void chip_set_inst(chip_t *c, const chip_inst_t *inst);
void chip_set_preset(chip_t *c, unsigned index);
void chip_set_mode(chip_t *c, chip_mode_t mode);
void chip_set_flavor(chip_t *c, chip_flavor_t flavor);
FM1_AUDIO_CODE void chip_note_on(chip_t *c, int note, int velocity);
FM1_AUDIO_CODE void chip_note_off(chip_t *c, int note);
FM1_AUDIO_CODE void chip_all_off(chip_t *c);
/* Handle one packed MIDI event: status | data1 << 8 | data2 << 16 (channel ignored). */
FM1_AUDIO_CODE void chip_midi_event(chip_t *c, uint32_t event);
#ifdef FM1_HOST_TEST
FM1_AUDIO_CODE void chip_render_s16(chip_t *c, int16_t *out, unsigned frames);
#endif
FM1_AUDIO_CODE void chip_render_q23_add(chip_t *c, int32_t *lr, unsigned frames);
int chip_active_voices(const chip_t *c);
FM1_AUDIO_CODE uint16_t chip_lfsr_step(uint16_t lfsr, int short_mode);
FM1_AUDIO_CODE uint16_t chip_sn_lfsr_step(uint16_t lfsr, int periodic);
extern const int16_t chip_sn_vol[16];

extern const chip_inst_t chip_presets[];
extern const int chip_num_presets;

/* ---- App-layer glue ---------------------------------------------------- */
/* Control or routing task. Call before the audio interrupt is attached. */
void chip_engine_init(int sample_rate);
/* Queue a short MIDI message from `lane` 0 (control task) or 1 (routing task);
 * each lane must have a single producer. Returns 1 if the message belongs to the
 * chip channel and was consumed, 0 otherwise. Reset (0xFF) is queued too but
 * returns 0 so the caller keeps handling it. */
int chip_engine_midi(unsigned lane, const uint8_t *message, size_t bytes);
/* Any task: silence everything at the next audio block. */
void chip_engine_panic(void);
/* Audio ISR, once per block, after the sealed engine rendered into `output`
 * (FM1_BLOCK_WORDS interleaved Q23 words). Adds the chip voices, saturating. */
FM1_AUDIO_CODE void chip_engine_render_add(int32_t *output, unsigned frames);
const chip_t *chip_engine_state(void);

#endif
