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
static const uint8_t gb_bass_wave[32] FM1_AUDIO_DATA = {
    8, 9, 11, 12, 13, 14, 14, 15, 15, 14, 14, 13, 12, 11, 9, 8,
    7, 6, 4, 3, 2, 1, 1, 0, 0, 1, 1, 2, 3, 4, 6, 7 };

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

    { .name = "GB Wave Bass", .wave = CHIP_WAVE, .wavetable = gb_bass_wave,
      .sustain = 15, .release_frames = 2 },

    { .name = "Noise Hat", .wave = CHIP_NOISE,
      .decay_frames = 1, .sustain = 0, .release_frames = 1 },

    { .name = "Noise Snare", .wave = CHIP_NOISE,
      .decay_frames = 2, .sustain = 0, .release_frames = 1 },

    { .name = "Metal Noise", .wave = CHIP_NOISE, .noise_short = 1,
      .decay_frames = 3, .sustain = 4, .release_frames = 2 },
};

const int chip_num_presets FM1_AUDIO_DATA = (int)(sizeof chip_presets / sizeof chip_presets[0]);
