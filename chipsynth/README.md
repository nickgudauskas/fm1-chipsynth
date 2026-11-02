# chipsynth: NES / Sega Genesis PSG voices for FM1 Open V14

A polyphonic chip engine (pulse with 4 duties, 32-step triangle, 4-bit wave table, LFSR noise; or the Genesis SN76489 PSG: 50% squares, 2 dB volume steps, Sega 16-bit noise) that plays alongside the instrument engine. It takes notes from one MIDI channel, so the FM1's own keys, USB/BLE/DIN MIDI and a DAW can all play it.

## How it fits this kit

The device's DSP engine and sequencer are the sealed `sdk/lib/libfm1_engine.a` (hash-checked by `Build.ps1`), so a new voice type can't be added *inside* it. The app layer, though, owns the last step of the audio path: `app/board/audio.c` calls `fm1_engine_render()` and then `fm1_output_mix()`. This adds one call between them:

```
fm1_engine_render(rendered)        sealed engine: tracks, FX
chip_engine_render_add(rendered)   <- new: chip voices, saturating add into the same Q23 block
fm1_output_mix(rendered)           master gain, USB / Bluetooth mixing (unchanged)
```

Notes arrive through `app/midi_router.c`: messages on MIDI channel 16 are claimed by the chip engine before they reach the track engines. Two single-producer queues (control task, routing task) are drained at the start of each audio block.

## Files

| Path | Purpose |
|---|---|
| `app/chip_engine.h`, `app/chip_engine.c`, `app/chip_presets.c` | The engine. Copy into the kit's `app/` (`Build.ps1` compiles `app/**/*.c` automatically). |
| `fm1-chip-engine.patch` | Four small edits: `app/board/audio.c` (init, render hook, panic on suspend) and `app/midi_router.c` (channel tee). |
| `host/` | Host tests and a demo renderer. They run on a normal computer against the real SDK headers. |

Apply from a **properly extracted** kit root (see "Folder note" below):

```
cp chipsynth/app/chip_*.[ch] app/
patch -p1 < chipsynth/fm1-chip-engine.patch
```

## Playing it

1. On SEQ page **8/8 TRACK ROLE**, give any track input MIDI channel **16** (K2). That track's own engine then receives nothing (the chip engine consumes channel 16), so it stays silent and just acts as the key selector.
2. Select that track and play the keys. From a DAW or controller, send on channel 16.

| MIDI on ch. 16 | Effect |
|---|---|
| Note on/off | Play |
| Program Change 0-24 | Preset: 0 Classic Lead, 1 PWM Lead, 2 Tri Bass, 3 Pluck, 4 Arp Chord, 5 Zap, 6 Coin, 7 Wave Bass, 8 Noise Hat, 9 Noise Snare, 10 Metal Noise, 11 Drum Kit, 12 Konami Lead, 13 Arp Major, 14 Arp Minor, 15 Arp Maj7, 16 Arp Octave, 17 Pulse Bass, 18 1-Up, 19 Power Up, 20 Jump, 21 Laser, 22 Explosion, 23 Chime, 24 Tremolo Lead |
| CC 70 | Flavor: below 64 NES (2A03 duties, triangle, nonlinear mixer), 64 and up Sega Genesis PSG (SN76489: every voice a 50% square, 2 dB steps, white or periodic noise) |
| CC 71 | Mode: 0-42 poly, 43-85 mono legato, 86-127 chip (2 pulse, 1 triangle, noise for notes below C2) |
| CC 72 | Mono portamento rate |
| CC 7 | Chip volume |
| Pitch bend | plus or minus 2 semitones |
| CC 120 / 123 | All sound off / all notes off |

**Drum Kit (11)** plays a different drum on each key, the same in every octave (General MIDI layout): C kick, C# and D# clap, D and E snare, F / G / A low, mid and high tom, F# and G# closed hat, A# open hat, B crash. Each drum has its own fixed pitch, so any octave works.

Tunables in `chip_engine.h`: `CHIP_MIDI_CHANNEL`, `CHIP_OUT_SHIFT` (level into the mix).

## Presets (projects)

`presets/NES.fm1` and `presets/GENESIS.fm1` are ready-made projects with all 8 tracks on DX7 FM:

| Track | NES | GENESIS |
|---|---|---|
| 1 | SQ LEAD | GN LEAD |
| 2 | PWM LD | GN BRAS |
| 3 | TRI BAS | GN SLAP |
| 4 | HAT | GN KICK |
| 5 | SNARE | GN SNAR |
| 6 | CHORD | GN EPNO |
| 7 | COIN | GN STRG |
| 8 | METAL | GN ORCH |

Load one in the Workbench (Projects & sounds, restore project file, or the simulator's file import), then save it to a USER slot. Rebuild them with `node chipsynth/presets/Make-Presets.mjs` after `tools/Build-Web.ps1 -LocalPreview`; the script drives the simulator core, so the files come from the panel's own export code.

The DX7 bank (`tools/Generate-Factory-DX7.mjs`) holds the voices: slots 0-9 NES-style, 10-21 Genesis YM2612-style (4-operator style FM: slap bass, synth bass, lead, brass, e-piano, bell, orchestra hit, kick, snare, tom, strings, organ), 22-53 32 voices of the V15 bank.

## Host tests

```
cd chipsynth/host
make -f host.mk SDK=/path/to/extracted/kit check   # 30 checks
make -f host.mk SDK=/path/to/extracted/kit demo_host && ./demo_host   # writes demo.wav
```

## Status: what is and isn't verified

Verified on a host compiler against the real SDK headers: LFSR periods, duty ratios, pitch accuracy (table within 0.002 cent, interpolation within 0.75 cent), poly/mono/chip allocation, queue handling including overflow, saturation against a full-scale host block, determinism, no float/libm in the engine sources, and the patched `midi_router.c` and `audio.c` pass a syntax check. The patch applies cleanly to the pristine files.

**Not verified: anything that needs the PI32 toolchain or the hardware.**
- No firmware build: `Build.ps1` needs Windows x64 and JieLi's PI32v2 clang, so the real compile, link and RAM/flash fit are untested.
- ISR time: the engine runs inside the audio interrupt. Read `fm1_audio_stats_read()` (`peak_ticks`, `overruns`) with 8 voices playing before trusting it.
- The audio path avoids 64-bit math and division on purpose (the SDK says every audio callee must be Flash-write safe), but that is by inspection, not by disassembly.
- Whether the panel lets you pick a track on channel 16 exactly as described above.

## Folder note

This copy lives inside a clone of github.com/m-vave/fm1_open (V14, with the JieLi 2.5.3 and named-register commits), so `app/`, `sdk/` and the rest are real folders. The patch in this folder was generated against that clone and applies with no offsets.

## License

Written to match the app layer (GPL-3.0-only with the FM1 SDK linking exception). The NES mixer curves are the commonly documented 2A03 formulas.
