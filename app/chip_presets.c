/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "chip_engine.h"

/* Read by the audio path (macros, wavetable): keep it where Flash writes can't stall it.
 * Macros (CHIP_MACRO: length, loop, values) and wavetables live in their own
 * arrays, so a preset that doesn't use one costs a null pointer. */

CHIP_MACRO(pwm_duty, 0, 0, 1, 2, 3, 2, 1);
CHIP_MACRO(pluck_duty, 0xFF, 2, 2, 1, 0);
CHIP_MACRO(chord_arp, 0, 0, 4, 7);
CHIP_MACRO(zap_arp, 0xFF, 24, 19, 15, 12, 9, 6, 3, 0);
CHIP_MACRO(coin_arp, 0xFF, 0, 0, 5);
CHIP_MACRO(arp_major, 0, 0, 4, 7);
CHIP_MACRO(arp_minor, 0, 0, 3, 7);
CHIP_MACRO(arp_maj7, 0, 0, 4, 7, 11);
CHIP_MACRO(arp_octave, 0, 0, 12);
CHIP_MACRO(konami_duty, 0xFF, 2, 2, 1);
CHIP_MACRO(konami_scoop, 0xFF, -16, -8, -3, 0);   /* 1/16 semitone */
CHIP_MACRO(oneup_arp, 0xFF, 0, 4, 7, 12, 16, 19, 24);
CHIP_MACRO(powerup_arp, 0xFF, 0, 5, 7, 12, 17, 19, 24, 29, 31, 36);
CHIP_MACRO(jump_arp, 0xFF, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
CHIP_MACRO(laser_arp, 0xFF, 0, -3, -6, -9, -12, -15, -18, -21, -24);
CHIP_MACRO(boom_arp, 0xFF, 0, -2, -4, -6, -8, -10, -12, -14, -16);
CHIP_MACRO(chime_arp, 0xFF, 12, 0);
CHIP_MACRO(tremolo_vol, 0, 15, 15, 8, 8);
/* Drum parts: one-shots that sweep down, as NES games built drums. */
CHIP_MACRO(kick_arp, 0xFF, 24, 12, 5, 0, -5, -9, -12);
CHIP_MACRO(tom_arp, 0xFF, 7, 4, 2, 0, -2, -4, -6);
CHIP_MACRO(snare_arp, 0xFF, 12, 8, 4, 0);
CHIP_MACRO(arp_sus4, 0, 0, 5, 7);
CHIP_MACRO(arp_dim, 0, 0, 3, 6, 9);
CHIP_MACRO(arp_dom7, 0, 0, 4, 7, 10);
CHIP_MACRO(siren_arp, 0, 0, 1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1);
CHIP_MACRO(pickup_arp, 0xFF, 0, 7, 12);
CHIP_MACRO(hurt_arp, 0xFF, 0, -1, 0, -2, 0, -3, -5, -8);
CHIP_MACRO(psg_kick_arp, 0xFF, 12, 5, 0, -7, -12, -17, -24);
static const uint8_t bass_wave[32] FM1_AUDIO_DATA = {
    8, 9, 11, 12, 13, 14, 14, 15, 15, 14, 14, 13, 12, 11, 9, 8,
    7, 6, 4, 3, 2, 1, 1, 0, 0, 1, 1, 2, 3, 4, 6, 7 };

static const chip_inst_t kick FM1_AUDIO_DATA = { CHIP_NAME("Kick") .wave = CHIP_TRI,
    .decay_frames = 1, .sustain = 0, .release_frames = 1, .arp = kick_arp };
static const chip_inst_t snare FM1_AUDIO_DATA = { CHIP_NAME("Snare") .wave = CHIP_NOISE,
    .decay_frames = 2, .sustain = 0, .release_frames = 1, .arp = snare_arp };
static const chip_inst_t clap FM1_AUDIO_DATA = { CHIP_NAME("Clap") .wave = CHIP_NOISE, .noise_short = 1,
    .decay_frames = 1, .sustain = 0, .release_frames = 1 };
static const chip_inst_t tom FM1_AUDIO_DATA = { CHIP_NAME("Tom") .wave = CHIP_TRI,
    .decay_frames = 3, .sustain = 0, .release_frames = 2, .arp = tom_arp };
static const chip_inst_t hat FM1_AUDIO_DATA = { CHIP_NAME("Closed Hat") .wave = CHIP_NOISE,
    .decay_frames = 1, .sustain = 0, .release_frames = 1 };
static const chip_inst_t open_hat FM1_AUDIO_DATA = { CHIP_NAME("Open Hat") .wave = CHIP_NOISE,
    .decay_frames = 3, .sustain = 0, .release_frames = 2 };
static const chip_inst_t crash FM1_AUDIO_DATA = { CHIP_NAME("Crash") .wave = CHIP_NOISE,
    .decay_frames = 7, .sustain = 0, .release_frames = 4 };
/* General MIDI layout within each octave: C kick, C# clap, D snare, D# clap,
 * E snare, F/G/A toms, F# and G# closed hat, A# open hat, B crash. */
static const struct chip_kit drum_kit FM1_AUDIO_DATA = {
    .part = { &kick, &clap, &snare, &clap, &snare, &tom, &hat, &tom, &hat, &tom, &open_hat, &crash },
    .note = { 48,    84,    72,     88,    76,     43,   96,   47,   98,   52,   96,        86 },
};

/* Genesis PSG kit: the SN76489 has no triangle, so kick and toms are squares. */
static const chip_inst_t psg_kick FM1_AUDIO_DATA = { CHIP_NAME("PSG Kick") .wave = CHIP_PULSE, .duty = 2,
    .decay_frames = 1, .sustain = 0, .release_frames = 1, .arp = psg_kick_arp };
static const chip_inst_t psg_tom FM1_AUDIO_DATA = { CHIP_NAME("PSG Tom") .wave = CHIP_PULSE, .duty = 2,
    .decay_frames = 2, .sustain = 0, .release_frames = 2, .arp = tom_arp };
static const struct chip_kit psg_kit FM1_AUDIO_DATA = {
    .part = { &psg_kick, &clap, &snare, &clap, &snare, &psg_tom, &hat, &psg_tom, &hat, &psg_tom, &open_hat, &crash },
    .note = { 40,        84,    72,     88,    76,     45,       96,   50,       98,   55,       96,        86 },
};

const chip_inst_t chip_presets[] FM1_AUDIO_DATA = {
    { CHIP_NAME("Classic Lead") .wave = CHIP_PULSE, .duty = 1,
      .sustain = 12, .release_frames = 3,
      .vib_delay = 12, .vib_speed = 40, .vib_depth = 3 },

    { CHIP_NAME("PWM Lead") .wave = CHIP_PULSE, .duty = 1,
      .sustain = 13, .release_frames = 3, .macro_div = 6,
      .duty_m = pwm_duty,
      .vib_delay = 20, .vib_speed = 36, .vib_depth = 2 },

    { CHIP_NAME("Tri Bass") .wave = CHIP_TRI,
      .sustain = 15, .release_frames = 2 },

    { CHIP_NAME("Pluck") .wave = CHIP_PULSE, .duty = 2,
      .decay_frames = 2, .sustain = 0, .release_frames = 1,
      .duty_m = pluck_duty },

    { CHIP_NAME("Arp Chord") .wave = CHIP_PULSE, .duty = 1,
      .sustain = 11, .release_frames = 2, .macro_div = 2,
      .arp = chord_arp },

    { CHIP_NAME("Zap") .wave = CHIP_PULSE, .duty = 2,
      .decay_frames = 1, .sustain = 0, .release_frames = 1,
      .arp = zap_arp },

    { CHIP_NAME("Coin") .wave = CHIP_PULSE, .duty = 2,
      .decay_frames = 4, .sustain = 0, .release_frames = 2, .macro_div = 3,
      .arp = coin_arp },

    { CHIP_NAME("Wave Bass") .wave = CHIP_WAVE, .wavetable = bass_wave,
      .sustain = 15, .release_frames = 2 },

    { CHIP_NAME("Noise Hat") .wave = CHIP_NOISE,
      .decay_frames = 1, .sustain = 0, .release_frames = 1 },

    { CHIP_NAME("Noise Snare") .wave = CHIP_NOISE,
      .decay_frames = 2, .sustain = 0, .release_frames = 1 },

    { CHIP_NAME("Metal Noise") .wave = CHIP_NOISE, .noise_short = 1,
      .decay_frames = 3, .sustain = 4, .release_frames = 2 },

    /* 11- added presets; program numbers 0-10 above are unchanged. */
    { CHIP_NAME("Drum Kit") .kit = &drum_kit },

    { CHIP_NAME("Konami Lead") .wave = CHIP_PULSE, .duty = 1,   /* 50% bite, then 25%, scooped in */
      .decay_frames = 4, .sustain = 10, .release_frames = 3, .macro_div = 2,
      .duty_m = konami_duty, .pitch = konami_scoop,
      .vib_delay = 10, .vib_speed = 48, .vib_depth = 4 },

    { CHIP_NAME("Arp Major") .wave = CHIP_PULSE, .duty = 1,
      .sustain = 11, .release_frames = 2, .arp = arp_major },

    { CHIP_NAME("Arp Minor") .wave = CHIP_PULSE, .duty = 1,
      .sustain = 11, .release_frames = 2, .arp = arp_minor },

    { CHIP_NAME("Arp Maj7") .wave = CHIP_PULSE, .duty = 2,
      .sustain = 11, .release_frames = 2, .arp = arp_maj7 },

    { CHIP_NAME("Arp Octave") .wave = CHIP_PULSE, .duty = 0,
      .sustain = 11, .release_frames = 2, .macro_div = 2, .arp = arp_octave },

    { CHIP_NAME("Pulse Bass") .wave = CHIP_PULSE, .duty = 2,
      .decay_frames = 2, .sustain = 8, .release_frames = 1 },

    { CHIP_NAME("1-Up") .wave = CHIP_PULSE, .duty = 1,
      .decay_frames = 6, .sustain = 0, .release_frames = 2, .macro_div = 4,
      .arp = oneup_arp },

    { CHIP_NAME("Power Up") .wave = CHIP_PULSE, .duty = 2,
      .decay_frames = 4, .sustain = 0, .release_frames = 2, .macro_div = 2,
      .arp = powerup_arp },

    { CHIP_NAME("Jump") .wave = CHIP_PULSE, .duty = 2,
      .decay_frames = 2, .sustain = 0, .release_frames = 1, .arp = jump_arp },

    { CHIP_NAME("Laser") .wave = CHIP_PULSE, .duty = 0,
      .decay_frames = 1, .sustain = 0, .release_frames = 1, .arp = laser_arp },

    { CHIP_NAME("Explosion") .wave = CHIP_NOISE,
      .decay_frames = 6, .sustain = 0, .release_frames = 3, .macro_div = 3,
      .arp = boom_arp },

    { CHIP_NAME("Chime") .wave = CHIP_PULSE, .duty = 0,
      .decay_frames = 6, .sustain = 0, .release_frames = 4, .macro_div = 3,
      .arp = chime_arp, .vib_delay = 20, .vib_speed = 30, .vib_depth = 2 },

    { CHIP_NAME("Tremolo Lead") .wave = CHIP_PULSE, .duty = 1,
      .sustain = 13, .release_frames = 3, .macro_div = 3, .vol = tremolo_vol },

    /* 25- */
    { CHIP_NAME("PSG Kit") .kit = &psg_kit },

    { CHIP_NAME("Arp Sus4") .wave = CHIP_PULSE, .duty = 1,
      .sustain = 11, .release_frames = 2, .arp = arp_sus4 },

    { CHIP_NAME("Arp Dim") .wave = CHIP_PULSE, .duty = 1,
      .sustain = 11, .release_frames = 2, .arp = arp_dim },

    { CHIP_NAME("Arp Dom7") .wave = CHIP_PULSE, .duty = 2,
      .sustain = 11, .release_frames = 2, .arp = arp_dom7 },

    { CHIP_NAME("Square Bass") .wave = CHIP_PULSE, .duty = 2,
      .decay_frames = 3, .sustain = 10, .release_frames = 1 },

    { CHIP_NAME("Vibrato Lead") .wave = CHIP_PULSE, .duty = 1,
      .sustain = 12, .release_frames = 3, .vib_speed = 64, .vib_depth = 8 },

    { CHIP_NAME("Siren") .wave = CHIP_PULSE, .duty = 2,
      .sustain = 12, .release_frames = 2, .macro_div = 2, .arp = siren_arp },

    { CHIP_NAME("Pickup") .wave = CHIP_PULSE, .duty = 1,
      .decay_frames = 4, .sustain = 0, .release_frames = 2, .macro_div = 3, .arp = pickup_arp },

    { CHIP_NAME("Hurt") .wave = CHIP_PULSE, .duty = 0,
      .decay_frames = 1, .sustain = 0, .release_frames = 1, .arp = hurt_arp },
};

const int chip_num_presets FM1_AUDIO_DATA = (int)(sizeof chip_presets / sizeof chip_presets[0]);
