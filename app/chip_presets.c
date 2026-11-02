/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "chip_engine.h"

/* Read by the audio path (macros, wavetable): keep it where Flash writes can't stall it.
 * Macro values and wavetables live in their own arrays so presets that don't use
 * them cost a pointer, not a fixed-size buffer. */
#define MACRO(values, loop_at) { values, (uint8_t)sizeof values, loop_at }

static const int8_t pwm_duty[] FM1_AUDIO_DATA = { 0, 1, 2, 3, 2, 1 };
static const int8_t pluck_duty[] FM1_AUDIO_DATA = { 2, 2, 1, 0 };
static const int8_t chord_arp[] FM1_AUDIO_DATA = { 0, 4, 7 };
static const int8_t zap_arp[] FM1_AUDIO_DATA = { 24, 19, 15, 12, 9, 6, 3, 0 };
static const int8_t coin_arp[] FM1_AUDIO_DATA = { 0, 0, 5 };
static const int8_t arp_major[] FM1_AUDIO_DATA = { 0, 4, 7 };
static const int8_t arp_minor[] FM1_AUDIO_DATA = { 0, 3, 7 };
static const int8_t arp_maj7[] FM1_AUDIO_DATA = { 0, 4, 7, 11 };
static const int8_t arp_octave[] FM1_AUDIO_DATA = { 0, 12 };
static const int8_t konami_duty[] FM1_AUDIO_DATA = { 2, 2, 1 };
static const int8_t konami_scoop[] FM1_AUDIO_DATA = { -16, -8, -3, 0 };   /* 1/16 semitone */
static const int8_t oneup_arp[] FM1_AUDIO_DATA = { 0, 4, 7, 12, 16, 19, 24 };
static const int8_t powerup_arp[] FM1_AUDIO_DATA = { 0, 5, 7, 12, 17, 19, 24, 29, 31, 36 };
static const int8_t jump_arp[] FM1_AUDIO_DATA = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
static const int8_t laser_arp[] FM1_AUDIO_DATA = { 0, -3, -6, -9, -12, -15, -18, -21, -24 };
static const int8_t boom_arp[] FM1_AUDIO_DATA = { 0, -2, -4, -6, -8, -10, -12, -14, -16 };
static const int8_t chime_arp[] FM1_AUDIO_DATA = { 12, 0 };
static const int8_t tremolo_vol[] FM1_AUDIO_DATA = { 15, 15, 8, 8 };
/* Drum parts: one-shots that sweep down, as NES games built drums. */
static const int8_t kick_arp[] FM1_AUDIO_DATA = { 24, 12, 5, 0, -5, -9, -12 };
static const int8_t tom_arp[] FM1_AUDIO_DATA = { 7, 4, 2, 0, -2, -4, -6 };
static const int8_t snare_arp[] FM1_AUDIO_DATA = { 12, 8, 4, 0 };
static const uint8_t bass_wave[32] FM1_AUDIO_DATA = {
    8, 9, 11, 12, 13, 14, 14, 15, 15, 14, 14, 13, 12, 11, 9, 8,
    7, 6, 4, 3, 2, 1, 1, 0, 0, 1, 1, 2, 3, 4, 6, 7 };

static const chip_inst_t kick FM1_AUDIO_DATA = { .name = "Kick", .wave = CHIP_TRI,
    .decay_frames = 1, .sustain = 0, .release_frames = 1, .arp = MACRO(kick_arp, 0xFF) };
static const chip_inst_t snare FM1_AUDIO_DATA = { .name = "Snare", .wave = CHIP_NOISE,
    .decay_frames = 2, .sustain = 0, .release_frames = 1, .arp = MACRO(snare_arp, 0xFF) };
static const chip_inst_t clap FM1_AUDIO_DATA = { .name = "Clap", .wave = CHIP_NOISE, .noise_short = 1,
    .decay_frames = 1, .sustain = 0, .release_frames = 1 };
static const chip_inst_t tom FM1_AUDIO_DATA = { .name = "Tom", .wave = CHIP_TRI,
    .decay_frames = 3, .sustain = 0, .release_frames = 2, .arp = MACRO(tom_arp, 0xFF) };
static const chip_inst_t hat FM1_AUDIO_DATA = { .name = "Closed Hat", .wave = CHIP_NOISE,
    .decay_frames = 1, .sustain = 0, .release_frames = 1 };
static const chip_inst_t open_hat FM1_AUDIO_DATA = { .name = "Open Hat", .wave = CHIP_NOISE,
    .decay_frames = 3, .sustain = 0, .release_frames = 2 };
static const chip_inst_t crash FM1_AUDIO_DATA = { .name = "Crash", .wave = CHIP_NOISE,
    .decay_frames = 7, .sustain = 0, .release_frames = 4 };
/* General MIDI layout within each octave: C kick, C# clap, D snare, D# clap,
 * E snare, F/G/A toms, F# and G# closed hat, A# open hat, B crash. */
static const struct chip_kit drum_kit FM1_AUDIO_DATA = {
    .part = { &kick, &clap, &snare, &clap, &snare, &tom, &hat, &tom, &hat, &tom, &open_hat, &crash },
    .note = { 48,    84,    72,     88,    76,     43,   96,   47,   98,   52,   96,        86 },
};

const chip_inst_t chip_presets[] FM1_AUDIO_DATA = {
    { .name = "Classic Lead", .wave = CHIP_PULSE, .duty = 1,
      .sustain = 12, .release_frames = 3,
      .vib_delay = 12, .vib_speed = 40, .vib_depth = 3 },

    { .name = "PWM Lead", .wave = CHIP_PULSE, .duty = 1,
      .sustain = 13, .release_frames = 3, .macro_div = 6,
      .duty_m = MACRO(pwm_duty, 0),
      .vib_delay = 20, .vib_speed = 36, .vib_depth = 2 },

    { .name = "Tri Bass", .wave = CHIP_TRI,
      .sustain = 15, .release_frames = 2 },

    { .name = "Pluck", .wave = CHIP_PULSE, .duty = 2,
      .decay_frames = 2, .sustain = 0, .release_frames = 1,
      .duty_m = MACRO(pluck_duty, 0xFF) },

    { .name = "Arp Chord", .wave = CHIP_PULSE, .duty = 1,
      .sustain = 11, .release_frames = 2, .macro_div = 2,
      .arp = MACRO(chord_arp, 0) },

    { .name = "Zap", .wave = CHIP_PULSE, .duty = 2,
      .decay_frames = 1, .sustain = 0, .release_frames = 1,
      .arp = MACRO(zap_arp, 0xFF) },

    { .name = "Coin", .wave = CHIP_PULSE, .duty = 2,
      .decay_frames = 4, .sustain = 0, .release_frames = 2, .macro_div = 3,
      .arp = MACRO(coin_arp, 0xFF) },

    { .name = "Wave Bass", .wave = CHIP_WAVE, .wavetable = bass_wave,
      .sustain = 15, .release_frames = 2 },

    { .name = "Noise Hat", .wave = CHIP_NOISE,
      .decay_frames = 1, .sustain = 0, .release_frames = 1 },

    { .name = "Noise Snare", .wave = CHIP_NOISE,
      .decay_frames = 2, .sustain = 0, .release_frames = 1 },

    { .name = "Metal Noise", .wave = CHIP_NOISE, .noise_short = 1,
      .decay_frames = 3, .sustain = 4, .release_frames = 2 },

    /* 11- added presets; program numbers 0-10 above are unchanged. */
    { .name = "Drum Kit", .kit = &drum_kit },

    { .name = "Konami Lead", .wave = CHIP_PULSE, .duty = 1,   /* 50% bite, then 25%, scooped in */
      .decay_frames = 4, .sustain = 10, .release_frames = 3, .macro_div = 2,
      .duty_m = MACRO(konami_duty, 0xFF), .pitch = MACRO(konami_scoop, 0xFF),
      .vib_delay = 10, .vib_speed = 48, .vib_depth = 4 },

    { .name = "Arp Major", .wave = CHIP_PULSE, .duty = 1,
      .sustain = 11, .release_frames = 2, .arp = MACRO(arp_major, 0) },

    { .name = "Arp Minor", .wave = CHIP_PULSE, .duty = 1,
      .sustain = 11, .release_frames = 2, .arp = MACRO(arp_minor, 0) },

    { .name = "Arp Maj7", .wave = CHIP_PULSE, .duty = 2,
      .sustain = 11, .release_frames = 2, .arp = MACRO(arp_maj7, 0) },

    { .name = "Arp Octave", .wave = CHIP_PULSE, .duty = 0,
      .sustain = 11, .release_frames = 2, .macro_div = 2, .arp = MACRO(arp_octave, 0) },

    { .name = "Pulse Bass", .wave = CHIP_PULSE, .duty = 2,
      .decay_frames = 2, .sustain = 8, .release_frames = 1 },

    { .name = "1-Up", .wave = CHIP_PULSE, .duty = 1,
      .decay_frames = 6, .sustain = 0, .release_frames = 2, .macro_div = 4,
      .arp = MACRO(oneup_arp, 0xFF) },

    { .name = "Power Up", .wave = CHIP_PULSE, .duty = 2,
      .decay_frames = 4, .sustain = 0, .release_frames = 2, .macro_div = 2,
      .arp = MACRO(powerup_arp, 0xFF) },

    { .name = "Jump", .wave = CHIP_PULSE, .duty = 2,
      .decay_frames = 2, .sustain = 0, .release_frames = 1, .arp = MACRO(jump_arp, 0xFF) },

    { .name = "Laser", .wave = CHIP_PULSE, .duty = 0,
      .decay_frames = 1, .sustain = 0, .release_frames = 1, .arp = MACRO(laser_arp, 0xFF) },

    { .name = "Explosion", .wave = CHIP_NOISE,
      .decay_frames = 6, .sustain = 0, .release_frames = 3, .macro_div = 3,
      .arp = MACRO(boom_arp, 0xFF) },

    { .name = "Chime", .wave = CHIP_PULSE, .duty = 0,
      .decay_frames = 6, .sustain = 0, .release_frames = 4, .macro_div = 3,
      .arp = MACRO(chime_arp, 0xFF), .vib_delay = 20, .vib_speed = 30, .vib_depth = 2 },

    { .name = "Tremolo Lead", .wave = CHIP_PULSE, .duty = 1,
      .sustain = 13, .release_frames = 3, .macro_div = 3, .vol = MACRO(tremolo_vol, 0) },
};

const int chip_num_presets FM1_AUDIO_DATA = (int)(sizeof chip_presets / sizeof chip_presets[0]);
