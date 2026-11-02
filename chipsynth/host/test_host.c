/* Host tests for chip_engine. Built with -DFM1_HOST_TEST against the real SDK headers. */
#include "chip_engine.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR 44100
static int fails = 0;
#define CHECK(cond, ...) do { if (cond) printf("PASS  "); else { printf("FAIL  "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

static int16_t buf[SR * 2];
static int32_t q23[FM1_BLOCK_WORDS];

static void render_note(chip_inst_t *inst, int flavor, int note)
{
    chip_t c;
    chip_init(&c, SR);
    chip_set_flavor(&c, (chip_flavor_t)flavor);
    chip_set_inst(&c, inst);
    chip_note_on(&c, note, 127);
    chip_render_s16(&c, buf, SR * 2);
}

/* Push one message through the glue, as the MIDI router would. */
static int post(unsigned lane, unsigned status, unsigned a, unsigned b, unsigned bytes)
{
    uint8_t m[3] = { (uint8_t)status, (uint8_t)a, (uint8_t)b };
    return chip_engine_midi(lane, m, bytes);
}

int main(void)
{
    /* 1. LFSR periods */
    {
        uint16_t l = 1; int p = 0;
        do { l = chip_lfsr_step(l, 0); p++; } while (l != 1 && p < 100000);
        CHECK(p == 32767, "long-mode LFSR period = %d (expect 32767)", p);
        l = 1; p = 0;
        do { l = chip_lfsr_step(l, 1); p++; } while (l != 1 && p < 100000);
        CHECK(p == 93 || p == 31, "short-mode LFSR period = %d (expect 93 or 31)", p);
        l = 0x8000; p = 0;
        do { l = chip_sn_lfsr_step(l, 1); p++; } while (l != 0x8000 && p < 100000);
        CHECK(p == 16, "SN76489 periodic noise period = %d (expect 16)", p);
        l = 0x8000; p = 0;
        do { l = chip_sn_lfsr_step(l, 0); p++; } while (l != 0x8000 && p < 100000);
        CHECK(p > 30000 && p < 65536, "SN76489 white noise period = %d (Sega taps 0 and 3)", p);
        double worst_db = 0;
        for (int i = 2; i < 16; i++) {
            double db = 20.0 * log10((double)chip_sn_vol[i] / chip_sn_vol[i - 1]);
            if (fabs(db - 2.0) > worst_db) worst_db = fabs(db - 2.0);
        }
        CHECK(chip_sn_vol[0] == 0 && worst_db < 0.05, "SN76489 volume steps 2 dB apart (worst %.3f dB off), level 0 silent", worst_db);
    }

    /* 2. Integer pitch table accuracy */
    {
        chip_t c; chip_init(&c, SR);
        double worst = 0;
        for (int n = 0; n <= 127; n++) {
            double exact = 440.0 * pow(2.0, (n - 69) / 12.0) * 4294967296.0 / SR;
            double cents = fabs(1200.0 * log2(c.inc_tbl[n] / exact));
            if (cents > worst) worst = cents;
        }
        CHECK(worst < 0.5, "pitch table worst error %.4f cents over notes 0..127 (limit 0.5)", worst);
        /* Interpolated fractions between semitones, through the render path. */
        double worst_i = 0;
        for (int st = 12 * 256; st < 100 * 256; st += 37) {
            unsigned n = (unsigned)st >> 8, fr = (unsigned)st & 255u;
            uint32_t inc = c.inc_tbl[n] + (uint32_t)(((uint64_t)(c.inc_tbl[n + 1] - c.inc_tbl[n]) * fr) >> 8);
            double exact = 440.0 * pow(2.0, (st / 256.0 - 69) / 12.0) * 4294967296.0 / SR;
            double cents = fabs(1200.0 * log2(inc / exact));
            if (cents > worst_i) worst_i = cents;
        }
        CHECK(worst_i < 1.0, "interpolated pitch worst error %.3f cents (limit 1.0)", worst_i);
    }

    /* 3. Pulse duty ratios at 44.1 kHz */
    {
        const double expect[4] = { 0.125, 0.25, 0.5, 0.75 };
        for (int d = 0; d < 4; d++) {
            chip_inst_t in = { .name = "t", .wave = CHIP_PULSE, .duty = (uint8_t)d, .sustain = 15 };
            render_note(&in, CHIP_FLAVOR_NES, 69);
            int mn = 32767, mx = -32768;
            for (int i = SR; i < SR * 2; i++) { if (buf[i] < mn) mn = buf[i]; if (buf[i] > mx) mx = buf[i]; }
            int mid = (mn + mx) / 2, hi = 0;
            for (int i = SR; i < SR * 2; i++) if (buf[i] > mid) hi++;
            double r = hi / (double)SR;
            CHECK(fabs(r - expect[d]) < 0.01, "duty %d high ratio %.3f (expect %.3f)", d, r, expect[d]);
        }
        /* The SN76489 has no duty control: every tone voice is a 50% square. */
        for (int w = 0; w < 3; w++) {
            chip_inst_t in = { .name = "t", .wave = (uint8_t)(w == 0 ? CHIP_PULSE : w == 1 ? CHIP_TRI : CHIP_WAVE),
                               .duty = 0, .sustain = 15, .wavetable = chip_presets[7].wavetable };
            render_note(&in, CHIP_FLAVOR_SN, 69);
            int mn = 32767, mx = -32768;
            for (int i = SR; i < SR * 2; i++) { if (buf[i] < mn) mn = buf[i]; if (buf[i] > mx) mx = buf[i]; }
            int mid = (mn + mx) / 2, hi = 0;
            for (int i = SR; i < SR * 2; i++) if (buf[i] > mid) hi++;
            double r = hi / (double)SR;
            CHECK(fabs(r - 0.5) < 0.01, "SN flavor %s voice high ratio %.3f (expect 0.500)", w == 0 ? "pulse" : w == 1 ? "triangle" : "wave", r);
        }
    }

    /* 4. Measured pitch */
    {
        int notes[3] = { 69, 57, 81 }; int hz[3] = { 440, 220, 880 };
        for (int k = 0; k < 3; k++) {
            chip_inst_t in = { .name = "t", .wave = CHIP_PULSE, .duty = 2, .sustain = 15 };
            render_note(&in, CHIP_FLAVOR_SN, notes[k]);
            int mn = 32767, mx = -32768;
            for (int i = SR; i < SR * 2; i++) { if (buf[i] < mn) mn = buf[i]; if (buf[i] > mx) mx = buf[i]; }
            int mid = (mn + mx) / 2, edges = 0;
            for (int i = SR + 1; i < SR * 2; i++) if (buf[i - 1] <= mid && buf[i] > mid) edges++;
            CHECK(abs(edges - hz[k]) <= 1, "note %d measured %d Hz (expect %d)", notes[k], edges, hz[k]);
        }
    }

    /* 5. Poly stealing, mono legato, chip routing */
    {
        chip_t c; chip_init(&c, SR);
        for (int n = 40; n < 60; n++) chip_note_on(&c, n, 100);
        CHECK(chip_active_voices(&c) == CHIP_MAX_VOICES, "20 notes -> %d active voices (max %d)", chip_active_voices(&c), CHIP_MAX_VOICES);

        chip_init(&c, SR);
        chip_set_mode(&c, CHIP_MODE_MONO);
        chip_note_on(&c, 60, 100);
        chip_render_s16(&c, buf, SR / 10);
        unsigned f0 = c.v[0].frame;
        chip_note_on(&c, 64, 100);
        CHECK(c.v[0].note == 64 && f0 > 0 && c.v[0].frame == f0, "legato keeps envelope (frame %u -> %u)", f0, (unsigned)c.v[0].frame);
        chip_note_off(&c, 64);
        CHECK(c.v[0].note == 60 && c.v[0].gate, "mono returns to held note (note=%d)", c.v[0].note);
        chip_note_off(&c, 60);
        CHECK(!c.v[0].gate, "mono all keys up -> gate off");

        chip_init(&c, SR);
        chip_set_mode(&c, CHIP_MODE_CHIP);
        chip_note_on(&c, 24, 100);
        CHECK(c.v[3].active && c.v[3].type == CHIP_NOISE, "chip mode: note 24 -> noise channel");
        chip_note_on(&c, 60, 100); chip_note_on(&c, 64, 100); chip_note_on(&c, 67, 100); chip_note_on(&c, 72, 100);
        CHECK(c.v[0].active + c.v[1].active + c.v[2].active == 3 && c.v[2].type == CHIP_TRI,
              "chip mode: 4 tonal notes steal within 3 channels, ch3 stays triangle");
    }

    /* 6. Glue: channel filtering and the event queues */
    {
        chip_engine_init(SR);
        CHECK(post(0, 0x90 | CHIP_MIDI_CHANNEL, 69, 100, 3) == 1, "chip-channel note-on is claimed");
        CHECK(post(0, 0x90, 69, 100, 3) == 0, "other-channel note-on is left for the tracks");
        CHECK(post(1, 0xff, 0, 0, 1) == 0, "reset is queued but not claimed");
        post(1, 0xff, 0, 0, 1);          /* reset arrives after the note on lane 1 */
        memset(q23, 0, sizeof q23);
        chip_engine_render_add(q23, FM1_BLOCK_FRAMES);
        CHECK(chip_active_voices(chip_engine_state()) == 0, "reset silences the engine at the next block");

        post(0, 0x90 | CHIP_MIDI_CHANNEL, 69, 100, 3);
        memset(q23, 0, sizeof q23);
        for (int b = 0; b < 8; b++) chip_engine_render_add(q23, FM1_BLOCK_FRAMES);
        int nz = 0;
        for (unsigned i = 0; i < FM1_BLOCK_WORDS; i++) nz += q23[i] != 0;
        CHECK(nz > 0, "queued note renders audio into the block (%d nonzero words)", nz);

        post(0, 0xC0 | CHIP_MIDI_CHANNEL, 5, 0, 2);
        post(0, 0xE0 | CHIP_MIDI_CHANNEL, 0, 127, 3);       /* bend fully up */
        post(0, 0xB0 | CHIP_MIDI_CHANNEL, 70, 100, 3);      /* SN76489 (Genesis PSG) flavor */
        post(0, 0xB0 | CHIP_MIDI_CHANNEL, 71, 100, 3);      /* chip mode */
        chip_engine_render_add(q23, FM1_BLOCK_FRAMES);
        const chip_t *s = chip_engine_state();
        CHECK(s->inst == &chip_presets[5], "program change selects preset 5 (%s)", s->inst->name);
        CHECK(s->bend_q8 > 500 && s->bend_q8 <= 512, "pitch bend up = %d/256 semitone (about +2)", (int)s->bend_q8);
        CHECK(s->flavor == CHIP_FLAVOR_SN && s->mode == CHIP_MODE_CHIP, "CC70/CC71 set flavor and mode");

        /* Overflow: more than a queue's worth of events between blocks forces a safe reset. */
        chip_engine_init(SR);
        for (int i = 0; i < 70; i++) post(0, 0x90 | CHIP_MIDI_CHANNEL, 40 + (i % 40), 100, 3);
        chip_engine_render_add(q23, FM1_BLOCK_FRAMES);
        CHECK(chip_active_voices(chip_engine_state()) == 0, "queue overflow ends in silence, no stuck notes");

        /* Idle fast path leaves the host block untouched; saturation never wraps. */
        chip_engine_init(SR);
        for (unsigned i = 0; i < FM1_BLOCK_WORDS; i++) q23[i] = 1234;
        chip_engine_render_add(q23, FM1_BLOCK_FRAMES);
        int untouched = 1;
        for (unsigned i = 0; i < FM1_BLOCK_WORDS; i++) untouched &= q23[i] == 1234;
        CHECK(untouched, "idle engine does not touch the block");
        for (int n = 48; n < 56; n++) post(0, 0x90 | CHIP_MIDI_CHANNEL, n, 127, 3);
        for (unsigned i = 0; i < FM1_BLOCK_WORDS; i++) q23[i] = (i & 1) ? -8388608 : 8388607;
        for (int b = 0; b < 20; b++) {
            chip_engine_render_add(q23, FM1_BLOCK_FRAMES);
            for (unsigned i = 0; i < FM1_BLOCK_WORDS; i++)
                if (q23[i] > 8388607 || q23[i] < -8388608) { fails++; printf("FAIL  out of Q23 range\n"); return 1; }
            for (unsigned i = 0; i < FM1_BLOCK_WORDS; i++) q23[i] = (i & 1) ? -8388608 : 8388607;
        }
        CHECK(1, "saturating add stays inside Q23 against a full-scale host block");
    }

    /* 7. Levels: every preset on its own and an 8-voice stack stay inside Q23 headroom */
    {
        long worst = 0;
        for (int p = 0; p < chip_num_presets; p++) {
            chip_t c; chip_init(&c, SR); chip_set_preset(&c, (unsigned)p);
            chip_note_on(&c, 48 + (p % 5) * 3, 127);
            chip_render_s16(&c, buf, SR);
            for (int i = 0; i < SR; i++) if (abs(buf[i]) > worst) worst = abs(buf[i]);
        }
        CHECK(worst < 32767, "single-voice peak across presets = %ld / 32767", worst);
        chip_t c; chip_init(&c, SR);
        for (int n = 40; n < 48; n++) chip_note_on(&c, n, 127);
        chip_render_s16(&c, buf, SR / 2);
        long peak = 0; int clipped = 0;
        for (int i = 0; i < SR / 2; i++) { if (abs(buf[i]) > peak) peak = abs(buf[i]); clipped += abs(buf[i]) >= 32767; }
        printf("INFO  8 loud pulse voices: peak %ld, clipped samples %d (int16 scale, before CHIP_OUT_SHIFT)\n", peak, clipped);
    }

    /* 8. Determinism */
    {
        unsigned long h[2];
        for (int run = 0; run < 2; run++) {
            chip_t c; chip_init(&c, SR); chip_set_mode(&c, CHIP_MODE_CHIP);
            chip_note_on(&c, 24, 127); chip_note_on(&c, 60, 100); chip_note_on(&c, 64, 100);
            chip_render_s16(&c, buf, SR);
            unsigned long x = 1469598103u;
            for (int i = 0; i < SR; i++) x = (x ^ (uint16_t)buf[i]) * 16777619u;
            h[run] = x;
        }
        CHECK(h[0] == h[1], "two identical runs give identical audio (hash %lx)", h[0]);
    }

    /* 9. Every preset sounds in both flavors; kits map keys; arps cycle */
    {
        int silent = 0; long worst = 0;
        for (int f = 0; f < 2; f++)
            for (int p = 0; p < chip_num_presets; p++) {
                chip_t c; chip_init(&c, SR); chip_set_preset(&c, (unsigned)p);
                chip_set_flavor(&c, f ? CHIP_FLAVOR_SN : CHIP_FLAVOR_NES);
                chip_note_on(&c, 60, 127);
                chip_render_s16(&c, buf, SR / 2);
                long peak = 0;
                for (int i = 0; i < SR / 2; i++) if (abs(buf[i]) > peak) peak = abs(buf[i]);
                if (peak < 500) { silent++; printf("      silent: %s (%s)\n", chip_presets[p].name, f ? "SN" : "NES"); }
                if (peak > worst) worst = peak;
            }
        CHECK(silent == 0 && worst < 32767, "all %d presets sound in NES and SN flavors (peak %ld)", chip_num_presets, worst);

        int kit = -1;
        for (int p = 0; p < chip_num_presets; p++) if (chip_presets[p].kit) kit = p;
        CHECK(kit >= 0, "a drum kit preset exists (%s)", kit >= 0 ? chip_presets[kit].name : "none");
        if (kit >= 0) {
            const struct chip_kit *k = chip_presets[kit].kit;
            int ok = 1;
            for (int key = 24; key < 96; key++) {             /* every key, several octaves */
                chip_t c; chip_init(&c, SR); chip_set_preset(&c, (unsigned)kit);
                chip_note_on(&c, key, 100);
                const chip_voice_t *v = &c.v[0];
                const chip_inst_t *part = k->part[key % 12];
                if (!v->active || v->inst != part || v->note != k->note[key % 12] || v->type != part->wave) ok = 0;
                chip_note_off(&c, key);
                if (v->gate) ok = 0;                          /* note-off finds the voice by key */
            }
            CHECK(ok, "kit: each key plays its part at the part's own note, and note-off releases it");
        }

        int major = -1;
        for (int p = 0; p < chip_num_presets; p++) if (!strcmp(chip_presets[p].name, "Arp Major")) major = p;
        chip_t c; chip_init(&c, SR); chip_set_preset(&c, (unsigned)major);
        chip_note_on(&c, 60, 100);
        uint32_t seen[3] = { 0 }; int distinct = 0;
        for (int fr = 0; fr < 6; fr++) {
            chip_render_s16(&c, buf, SR / CHIP_FRAME_HZ);
            uint32_t inc = c.v[0].inc; int known = 0;
            for (int i = 0; i < distinct; i++) known |= seen[i] == inc;
            if (!known && distinct < 3) seen[distinct++] = inc;
        }
        uint32_t lo = seen[0] < seen[1] ? seen[0] : seen[1];
        lo = lo < seen[2] ? lo : seen[2];
        CHECK(major >= 0 && distinct == 3 && lo == c.inc_tbl[60], "Arp Major cycles 3 pitches from the played note");
    }

    printf(fails ? "\n%d test(s) FAILED\n" : "\nAll tests passed\n", fails);
    return fails ? 1 : 0;
}
