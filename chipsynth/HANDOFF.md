# Handoff: chipsynth for FM1 Open V14

Read this first, then `README.md`. It records where the work stands so a fresh session can continue.

## Goal
A playable NES / Game Boy style synth engine for the M-VAVE FM-1: press keys, get chip sounds (polyphonic, with mono-legato and a 3+noise "chip" mode).

## Key finding that shaped the design
The device's DSP engine and sequencer are the sealed binary `sdk/lib/libfm1_engine.a` (hash-checked in `sdk/Build.ps1`). A new engine can't live inside it. The open-source app layer owns the final audio step: `app/board/audio.c` calls `fm1_engine_render()` then `fm1_output_mix()`. The chip engine mixes into that block in between, and takes notes from MIDI channel 16 via a tee in `app/midi_router.c`.

## State
- Done: fixed-point engine (`app/chip_engine.[ch]`, `app/chip_presets.c`), integration patch (`fm1-chip-engine.patch`, generated against this clone, applies with no offsets), host tests (`host/test_host.c`, 30 checks + a no-float audit), demo renderer (`host/demo_host.c`).
- NOT applied: `app/` in this clone is still pristine. Apply with `cp chipsynth/app/chip_*.[ch] app/` then `patch -p1 < chipsynth/fm1-chip-engine.patch` from the clone root.
- Run the checks: `cd chipsynth/host && make -f host.mk check` (also `demo_host`). `make clean` can't delete without delete permission, so remove built binaries by hand.

## First Windows build (2026-11-02, Parallels Windows 11 ARM VM, JieLi 2.5.3 under x64 emulation)
- Toolchain: `C:\JL\pi32`, clang hash matches `README.md`. JieLi's installer needs JieLi Code::Blocks installed and opened once first.
- Stock firmware builds and packages: app image 453,576 bytes.
- The app image limit is 453,921 bytes (`sdk/Package.cs`: FlashLimit - UserSize - BtSize - MinimumVm - Area - AppData - 447), so stock has only 345 bytes free. The image counts `.text` plus SRAM-initialized `.ram0_data`, so moving tables between Flash and SRAM doesn't help.
- With chipsynth applied: compiles, links, SRAM placement checks pass, but packaging fails "Application does not fit the FM1 layout".
  - Before shrinking: 459,616 bytes (5,695 over). After `chip_render_s16` became host-only and presets/macros became pointers to separate tables: 457,636 bytes (3,715 over). Audio output unchanged (test hash `90142a4a37257d65`).
  - Chipsynth now costs about 4.0 KB, so it can't fit by shrinking it alone. Something in the stock app has to give (largest app item: `factory_packed`, 10.5 KB of compressed factory presets in `app/fm_factory.inc`; `screen.c` drawing code is the next candidate). Shrinking the user area would change the device's flash layout and is ruled out.
- RAM is fine: heap 236 KB vs 48 KB minimum.
- Where else bytes could come from (measured from `build/dx7/sdk.map`, non-SDK only): `screen.c` 40.0 KB (5.8 KB of it string literals), LTO'd app units 28.3 KB, `midi_router.c` 3.3 KB, `panel.c` 3.3 KB, `transfer.c` 2.8 KB.
  - Build flags are not a lever: `screen.c`, storage and factory units are already `-Oz`, and `sdk/Build.ps1` is hash-locked by `sdk/manifest.json`. `malloc`/`free` (5.5 KB) are SDK libc, not app.
  - Factory bank (`factory_packed`, 10,517 bytes for 16,384 raw): LZMA reaches 7,201 (ceiling, decoder too big); optimal-parse bit-level LZ with 7-bit literals reaches about 8,950 (saves about 1.5 KB, decoder about 150 bytes); per-field adaptive models do worse (only 128 voices per field).
  - `screen.c` strings are all uppercase ASCII: 6-bit packing would save about 1.3 KB but touches every text draw site.
  - Lossless total from both is roughly 2.8 to 3 KB, short of the 3.7 KB needed.

## Resolution: trimmed factory bank (2026-11-02) - firmware now packages
- `tools/Generate-Factory-DX7.mjs` now keeps 32 distinct voices of the V15 bank (variants 1-2 of the 12 families, plus BOWOAN, BDWHOCARES, TIBET, FINGERCYM, TIMPANI, STEEL DRUM, KALIMBA, BRUSHES). The other 96 slots copy a kept voice (same family for slots 0-95), so all 128 program numbers stay valid. `factory_packed` went from 10,517 to 3,415 bytes.
- `--syx DIR` writes the untouched bank as `FM1-V15-factory-A`..`D.syx` (checked byte-identical to the original). They're in `chipsynth/factory-syx/`. The web tool's DX7 library section ("replace this group of 32 voices from file", groups A-D) accepts 4104-byte bank .syx, so users can restore any original slot without firmware space. That upload path was checked in the code only, not on a device.
- `THIRD_PARTY.md` and the `.inc` header mark the bank as modified (Apache-2.0).
- Build with chipsynth applied: app image 450,532 bytes, 3,389 bytes under the 453,921 limit; `FM1_OFFICIAL_OPEN.fwsc` written (492,084 bytes); heap 238 KB.
- `app/` now has chipsynth applied (copied engine + patch); committed as e7d147a on branch `chipsynth`.

## Bank reorder: chip voices first (2026-11-02)
- Slots 0-10 are 11 new DX7 patches imitating the chipsynth presets (CHIP LEAD, CHIP PWM, CHIP TRI, CHIP PLUCK, CHIP CHORD, CHIP ZAP, CHIP COIN, GB WAVBASS, NOISE HAT, NOISE SNR, METALNOISE), defined in `tools/Generate-Factory-DX7.mjs`. Slots 11-42 are the 32 kept V15 voices; 43-127 repeat 0-42. This moves program numbers, so saved projects load different DX7 voices than on stock firmware.
- They are FM approximations (2:1 modulator = square, feedback = noise, pitch EG for zap/coin; one pitch-EG step = 1/32 octave). Single-tone voices stack three identical pairs to match V15 loudness.
- Checked by rendering through `web/synth` on the Mac: peaks 0.30-0.36 vs PIANO 1 0.29, pitches on note, noise voices 4.8-9.6k zero crossings/s. Rendered audio in `build/dx7-chip-voices.wav`. All 128 voices pass `FM1Web.validateVoice`. Not yet heard by a person.
- Firmware: 450,884 bytes, 3,037 free. Bank encodes to 3,754 bytes. `.inc` SPDX is now `Apache-2.0 AND GPL-3.0-only WITH GPL-3.0-linking-exception`.

## Flashable Workbench (2026-11-02)
- Repo: github.com/nickgudauskas/fm1-chipsynth (private; `origin`), M-VAVE's original is `upstream`.
- Firmware display name is **Chipwave**, version **V01** (`app/version.h`; the build requires V plus two or more digits; the update ID `FM-1_8000014` is left alone) (startup screen, HOME header, workbench label "Chipwave firmware"); the engine is still `chipsynth` in code. Changing `sdk/Update.html.in` / `sdk/I18n.js` requires updating their hashes in `sdk/manifest.json`.
- Built in the VM with `sdk\Build.ps1 -Package -Toolchain C:\JL\pi32 -SourceUrl https://github.com/nickgudauskas/fm1-chipsynth/archive/46deb8e945757ade91fe295027d554c22a5e4570.tar.gz` (the script only checks the URL format). Output: `build/official-web/FM1-Official-Open-Source.html` (SHA-256 `840cc215...` in `SHA256SUMS.txt`), build ID `5bc46dda...`. It embeds our firmware plus the stock V15 recovery image. Rebuild with a new commit's archive URL whenever `app/` changes.
- The source link points at a private repo, so the page is fine for personal flashing but must not be shared until the repo (or a release archive) is public.
- Local serving: `.claude/launch.json` config `workbench` serves it at http://localhost:8766/FM1-Official-Open-Source.html. Web MIDI needs a secure context; localhost qualifies.
- Not yet connected to or flashed onto a device.

## Constraints learned (don't relearn the hard way)
- Audio code must be Flash-write safe: tag code `FM1_AUDIO_CODE`, initialized/const tables `FM1_AUDIO_DATA`, and avoid libc/libm and anything that may pull `libcompiler_rt` from Flash. That is why the audio path has no float, no 64-bit math and no variable division (macros step with incremental counters). 64-bit math exists only in init (`build_tables`).
- Output is 44.1 kHz stereo, signed Q23, 128-frame blocks. The master gain is applied afterwards in `fm1_output_mix`.
- Firmware build needs Windows x64, PowerShell and the JieLi PI32v2 clang 4.0.1 (see `README.md` in the clone root). The user is on a Mac, so no firmware build has ever been run.
- Local keys reach the engines through `groove_midi_local()` using the selected track's input channel, so the intended workflow is: set a track's input channel to 16 (SEQ 8/8 TRACK ROLE, K2). Unverified on hardware.

## Verified vs not
Verified on a host compiler against the real SDK headers: engine behavior, queues and overflow, saturation, determinism, and that the patched `audio.c` / `midi_router.c` pass `gcc -fsyntax-only`.
Not verified: the PI32 compile/link and RAM fit (engine state is about 5 KB), interrupt CPU margin (read `fm1_audio_stats_read()` `peak_ticks` / `overruns` with 8 voices), the panel workflow above, and any listening comparison against real NES / Game Boy hardware.

## Open items / ideas (the user decides the order)
1. Done: real Windows build (see above). Still to do on hardware: flash the `.fwsc`, measure ISR time with `fm1_audio_stats_read()`, and listen to the trimmed DX7 bank.
2. 8 loud pulse voices clip slightly at int16 scale (14 samples in the test); consider a soft limiter or lower `CHIP_OUT_SHIFT`.
3. An on-device UI page (preset / mode / flavor) in `app/screen.c` instead of MIDI-only control (screen.c is about 150 KB; read how pages are registered first).
4. More presets and macros; per-preset stereo or detune.
5. Hardware rate is 44118 Hz (`FM1_AUDIO_HARDWARE_RATE`) vs the 44100 used for pitch; about 0.04 percent, probably ignorable.

## License
GPL-3.0-only with the FM1 SDK linking exception, matching the app layer.
