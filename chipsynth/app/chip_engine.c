/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "chip_engine.h"
#include <string.h>

/* 2^(+-1/12) in Q32. */
#define SEMI_UP   4550359342u
#define SEMI_DOWN 4053909305u
#define CHIP_QUEUE 64u /* power of two */

/* Pulse is high while the top 3 phase bits are below the threshold (of 8). */
static const uint8_t duty_thr[4] FM1_AUDIO_DATA = { 1, 2, 4, 6 };

FM1_AUDIO_CODE uint16_t chip_lfsr_step(uint16_t l, int short_mode)
{
    uint16_t fb = (uint16_t)((l ^ (l >> (short_mode ? 6 : 1))) & 1u);
    return (uint16_t)((l >> 1) | (fb << 14));
}

/* Control-context setup only (may take its time, not in the audio path). */
static void build_tables(chip_t *c)
{
    /* NES mixer curves, scaled so a few loud voices stay under full scale.
     * pulse: 95.88 / (8128/n + 100), tnd: 163.67 / (24329/n + 100). */
    c->pulse_tbl[0] = c->tnd_tbl[0] = 0;
    for (unsigned i = 1; i < CHIP_TBL_N; i++) {
        c->pulse_tbl[i] = (int32_t)((uint64_t)65536u * 9588u * i / (100ULL * (8128u + 100u * i)));
        c->tnd_tbl[i]   = (int32_t)((uint64_t)65536u * 16367u * i / (100ULL * (24329u + 100u * i)));
    }
    /* Phase increments from A4 = 440 Hz by repeated semitone ratios, rounded. */
    c->inc_tbl[69] = (uint32_t)(((uint64_t)440 << 32) / (uint32_t)c->sr);
    for (int n = 70; n <= 128; n++)
        c->inc_tbl[n] = (uint32_t)(((uint64_t)c->inc_tbl[n - 1] * SEMI_UP + 0x80000000u) >> 32);
    for (int n = 68; n >= 0; n--)
        c->inc_tbl[n] = (uint32_t)(((uint64_t)c->inc_tbl[n + 1] * SEMI_DOWN + 0x80000000u) >> 32);
}

void chip_init(chip_t *c, int sr)
{
    memset(c, 0, sizeof *c);
    c->sr = sr;
    c->flavor = CHIP_FLAVOR_NES;
    c->mode = CHIP_MODE_POLY;
    c->num_voices = CHIP_MAX_VOICES;
    c->gain_q8 = 256;
    c->inst = &chip_presets[0];
    for (int i = 0; i < CHIP_MAX_VOICES; i++) c->v[i].lfsr = 1;
    build_tables(c);
}

void chip_set_inst(chip_t *c, const chip_inst_t *inst) { c->inst = inst; }
void chip_set_flavor(chip_t *c, chip_flavor_t f) { c->flavor = f; }

void chip_set_preset(chip_t *c, unsigned index)
{
    if (index < (unsigned)chip_num_presets) c->inst = &chip_presets[index];
}

FM1_AUDIO_CODE void chip_all_off(chip_t *c)
{
    for (int i = 0; i < CHIP_MAX_VOICES; i++) c->v[i].active = 0;
    c->nheld = 0;
}

void chip_set_mode(chip_t *c, chip_mode_t m)
{
    chip_all_off(c);
    c->mode = m;
}

int chip_active_voices(const chip_t *c)
{
    int n = 0;
    for (int i = 0; i < CHIP_MAX_VOICES; i++) n += c->v[i].active;
    return n;
}

/* Macros advance one position per step with an incremental counter, so the audio
 * path never divides (no libcompiler_rt calls from Flash). pos is always < len. */
static FM1_AUDIO_CODE int macro_at(const chip_macro_t *m, uint8_t pos, int def)
{
    return m->len ? m->v[pos] : def;
}

static FM1_AUDIO_CODE void macro_advance(const chip_macro_t *m, uint8_t *pos)
{
    if (!m->len) return;
    if (++*pos >= m->len) *pos = (m->loop == 0xFF || m->loop >= m->len) ? (uint8_t)(m->len - 1) : m->loop;
}

static FM1_AUDIO_CODE uint32_t pitch_inc(const chip_t *c, int st_q8)
{
    if (st_q8 < 0) st_q8 = 0;
    if (st_q8 > 127 * 256 + 255) st_q8 = 127 * 256 + 255;
    unsigned n = (unsigned)st_q8 >> 8, frac = (unsigned)st_q8 & 255u;
    uint32_t a = c->inc_tbl[n], b = c->inc_tbl[n + 1];
    uint32_t d = b - a;                    /* < 2^27, so split to stay in 32 bits */
    return a + (d >> 8) * frac + (((d & 255u) * frac) >> 8);
}

/* Recompute macros, pitch and output level for the current frame. */
static FM1_AUDIO_CODE void voice_update(chip_t *c, chip_voice_t *v)
{
    const chip_inst_t *in = v->inst;
    int arp  = macro_at(&in->arp, v->mpos[0], 0);
    int mvol = macro_at(&in->vol, v->mpos[2], 15);
    int pit  = macro_at(&in->pitch, v->mpos[3], 0);
    v->duty  = (uint8_t)(macro_at(&in->duty_m, v->mpos[1], in->duty) & 3);
    if (mvol < 0) mvol = 0;
    if (mvol > 15) mvol = 15;
    v->level = (uint8_t)(v->env * mvol / 15);

    int vib_q8 = 0;
    if (in->vib_depth && v->frame >= in->vib_delay) {
        int t = v->vib_ph < 128 ? (int)v->vib_ph - 64 : 191 - (int)v->vib_ph;
        vib_q8 = (int)in->vib_depth * t / 4;     /* depth/16 semitone * t/64, in Q8 */
    }

    int32_t target = v->note * 256;
    int32_t g = c->glide_q8;
    if (g > 0 && v->pitch_q8 != target) {
        int32_t d = target - v->pitch_q8;
        if (d >= -g && d <= g) v->pitch_q8 = target;
        else v->pitch_q8 += d > 0 ? g : -g;
    } else {
        v->pitch_q8 = target;
    }

    int st = v->pitch_q8 + arp * 256 + c->bend_q8 + pit * 16 + vib_q8;
    v->inc = pitch_inc(c, st);

    if (v->type == CHIP_NOISE) {      /* pitched noise: LFSR clock = 64x note frequency */
        /* at most one clock per sample: cap just under 2^32 */
        v->ninc = v->inc > 67031250u ? 4290000000u : v->inc * 64u;
    }
}

static FM1_AUDIO_CODE void voice_tick(chip_t *c, chip_voice_t *v)
{
    const chip_inst_t *in = v->inst;
    if (v->gate) {
        if (in->decay_frames && v->env > v->sus && ++v->env_cnt >= in->decay_frames) {
            v->env--;
            v->env_cnt = 0;
        }
    } else {
        if (in->release_frames == 0) v->env = 0;
        else if (++v->env_cnt >= in->release_frames) {
            if (v->env) v->env--;
            v->env_cnt = 0;
        }
        if (v->env == 0) { v->active = 0; return; }
    }
    if (v->frame < 65535) v->frame++;
    if (++v->mdiv >= (in->macro_div ? in->macro_div : 1u)) {
        v->mdiv = 0;
        macro_advance(&in->arp, &v->mpos[0]);
        macro_advance(&in->duty_m, &v->mpos[1]);
        macro_advance(&in->vol, &v->mpos[2]);
        macro_advance(&in->pitch, &v->mpos[3]);
    }
    v->vib_ph = (uint8_t)(v->vib_ph + in->vib_speed);
    voice_update(c, v);
}

static FM1_AUDIO_CODE void voice_trigger(chip_t *c, chip_voice_t *v, int note, int vel,
                                         chip_wave_t type, int legato)
{
    const chip_inst_t *in = c->inst;
    int lvl = (vel * 15 + 126) / 127;
    if (lvl < 1) lvl = 1;
    if (lvl > 15) lvl = 15;

    int was_active = v->active;
    v->inst = in;
    v->type = (uint8_t)type;
    v->note = note;
    v->vel = (uint8_t)vel;
    v->age = ++c->age_ctr;
    if (!legato) {
        v->gate = 1;
        v->env = (uint8_t)lvl;
        v->sus = in->sustain < lvl ? in->sustain : (uint8_t)lvl;
        v->env_cnt = 0;
        v->frame = 0;
        v->mdiv = 0;
        v->mpos[0] = v->mpos[1] = v->mpos[2] = v->mpos[3] = 0;
        v->vib_ph = 0;
        v->phase = 0;
        v->nphase = 0;
        if (!v->lfsr) v->lfsr = 1;
        if (!was_active) v->pitch_q8 = note * 256;
    }
    v->active = 1;
    voice_update(c, v);
}

static FM1_AUDIO_CODE chip_voice_t *alloc_voice(chip_t *c, int lo, int hi)
{
    chip_voice_t *best = 0;
    for (int i = lo; i < hi; i++)
        if (!c->v[i].active) return &c->v[i];
    for (int i = lo; i < hi; i++) {            /* prefer releasing voices */
        chip_voice_t *v = &c->v[i];
        if (!v->gate && (!best || v->age < best->age)) best = v;
    }
    if (best) return best;
    for (int i = lo; i < hi; i++) {            /* else oldest held */
        chip_voice_t *v = &c->v[i];
        if (!best || v->age < best->age) best = v;
    }
    return best;
}

static FM1_AUDIO_CODE void held_remove(chip_t *c, int note)
{
    int j = 0;
    for (int i = 0; i < c->nheld; i++)
        if (c->held[i] != note) { c->held[j] = c->held[i]; c->held_vel[j] = c->held_vel[i]; j++; }
    c->nheld = j;
}

FM1_AUDIO_CODE void chip_note_on(chip_t *c, int note, int vel)
{
    if (note < 0 || note > 127) return;
    if (vel <= 0) { chip_note_off(c, note); return; }
    if (c->mode == CHIP_MODE_MONO) {
        held_remove(c, note);
        if (c->nheld < 8) { c->held[c->nheld] = (uint8_t)note; c->held_vel[c->nheld] = (uint8_t)vel; c->nheld++; }
        chip_voice_t *v = &c->v[0];
        int legato = v->active && v->gate;
        voice_trigger(c, v, note, vel, c->inst->wave, legato);
        return;
    }
    if (c->mode == CHIP_MODE_CHIP) {
        if (note < 36) {                       /* low notes drive the noise channel */
            voice_trigger(c, &c->v[3], note, vel, CHIP_NOISE, 0);
            return;
        }
        for (int i = 0; i < 3; i++)            /* same note held: retrigger in place */
            if (c->v[i].active && c->v[i].gate && c->v[i].note == note) {
                voice_trigger(c, &c->v[i], note, vel, i == 2 ? CHIP_TRI : CHIP_PULSE, 0);
                return;
            }
        chip_voice_t *v = alloc_voice(c, 0, 3);
        voice_trigger(c, v, note, vel, (v - c->v) == 2 ? CHIP_TRI : CHIP_PULSE, 0);
        return;
    }
    for (int i = 0; i < c->num_voices; i++)    /* poly */
        if (c->v[i].active && c->v[i].gate && c->v[i].note == note) {
            voice_trigger(c, &c->v[i], note, vel, c->inst->wave, 0);
            return;
        }
    voice_trigger(c, alloc_voice(c, 0, c->num_voices), note, vel, c->inst->wave, 0);
}

FM1_AUDIO_CODE void chip_note_off(chip_t *c, int note)
{
    if (c->mode == CHIP_MODE_MONO) {
        held_remove(c, note);
        chip_voice_t *v = &c->v[0];
        if (c->nheld > 0) {
            if (v->active && v->gate) v->note = c->held[c->nheld - 1];
        } else if (v->active && v->gate) {
            v->gate = 0;
            v->env_cnt = 0;
        }
        return;
    }
    chip_voice_t *best = 0;
    for (int i = 0; i < CHIP_MAX_VOICES; i++) {
        chip_voice_t *v = &c->v[i];
        if (v->active && v->gate && v->note == note && (!best || v->age < best->age)) best = v;
    }
    if (best) { best->gate = 0; best->env_cnt = 0; }
}

/* Packed event: status | d1 << 8 | d2 << 16. The channel nibble is ignored here:
 * channel filtering belongs to the glue. 0xFF is reset. */
FM1_AUDIO_CODE void chip_midi_event(chip_t *c, uint32_t ev)
{
    unsigned status = ev & 0xffu, d1 = (ev >> 8) & 127u, d2 = (ev >> 16) & 127u;
    if (status == 0xff) { chip_all_off(c); return; }
    switch (status & 0xf0u) {
    case 0x90: if (d2) chip_note_on(c, (int)d1, (int)d2); else chip_note_off(c, (int)d1); break;
    case 0x80: chip_note_off(c, (int)d1); break;
    case 0xC0: chip_set_preset(c, d1); break;
    case 0xE0: c->bend_q8 = ((int)(d1 | (d2 << 7)) - 8192) / 16; break;  /* +-2 semitones */
    case 0xB0:
        switch (d1) {
        case 7:   c->gain_q8 = d2 == 127u ? 256 : (int)d2 * 2; break;
        case 70:  c->flavor = d2 >= 64u ? CHIP_FLAVOR_GB : CHIP_FLAVOR_NES; break;
        case 71:  chip_set_mode(c, d2 < 43u ? CHIP_MODE_POLY : d2 < 86u ? CHIP_MODE_MONO : CHIP_MODE_CHIP); break;
        case 72:  c->glide_q8 = (int)d2 * 2; break;
        case 120: chip_all_off(c); break;
        case 123:
            for (int i = 0; i < CHIP_MAX_VOICES; i++)
                if (c->v[i].active && c->v[i].gate) { c->v[i].gate = 0; c->v[i].env_cnt = 0; }
            c->nheld = 0;
            break;
        default: break;
        }
        break;
    default: break;
    }
}

/* One output sample at int16 scale. Always inlined into the render loops so the
 * audio path needs no calls other than the 60 Hz voice ticks. */
static inline __attribute__((always_inline)) int32_t chip_sample(chip_t *c)
{
    c->frame_acc += CHIP_FRAME_HZ;
    if (c->frame_acc >= c->sr) {
        c->frame_acc -= c->sr;
        for (int k = 0; k < CHIP_MAX_VOICES; k++)
            if (c->v[k].active) voice_tick(c, &c->v[k]);
    }

    int psum = 0, tsum = 0, nsum = 0;
    for (int k = 0; k < CHIP_MAX_VOICES; k++) {
        chip_voice_t *v = &c->v[k];
        if (!v->active) continue;
        int lvl = v->level;
        switch (v->type) {
        case CHIP_PULSE:
            v->phase += v->inc;
            if ((v->phase >> 29) < duty_thr[v->duty]) psum += lvl;
            break;
        case CHIP_TRI: {
            v->phase += v->inc;
            int idx = (int)(v->phase >> 27);
            int s = idx < 16 ? idx : 31 - idx;
            tsum += s * lvl / 15;
            break; }
        case CHIP_WAVE:
            v->phase += v->inc;
            tsum += v->inst->wavetable[v->phase >> 27] * lvl / 15;
            break;
        case CHIP_NOISE: {
            uint32_t prev = v->nphase;
            v->nphase += v->ninc;
            if (v->nphase < prev) v->lfsr = chip_lfsr_step(v->lfsr, v->inst->noise_short);
            if (!(v->lfsr & 1)) nsum += lvl;
            break; }
        }
    }

    int32_t x;
    if (c->flavor == CHIP_FLAVOR_NES) {
        int pi = psum < CHIP_TBL_N ? psum : CHIP_TBL_N - 1;
        int ti = 3 * tsum + 2 * nsum;
        if (ti >= CHIP_TBL_N) ti = CHIP_TBL_N - 1;
        x = c->pulse_tbl[pi] + c->tnd_tbl[ti];
    } else {
        x = (psum + tsum + nsum) * 1000;
    }

    int32_t y = x - c->x1 + ((c->y1 * 32440) >> 15);   /* DC blocker, R ~ 0.99 */
    c->x1 = x;
    c->y1 = y;
    y = (y * c->gain_q8) >> 8;
    if (y > 32767) y = 32767;
    if (y < -32768) y = -32768;
    return y;
}

#ifdef FM1_HOST_TEST
/* Host tests and demo only: FM1_AUDIO_CODE marks functions used, so a firmware
 * copy would cost a second inlined chip_sample in SRAM for nothing. */
FM1_AUDIO_CODE void chip_render_s16(chip_t *c, int16_t *out, unsigned frames)
{
    for (unsigned i = 0; i < frames; i++) out[i] = (int16_t)chip_sample(c);
}
#endif

FM1_AUDIO_CODE void chip_render_q23_add(chip_t *c, int32_t *lr, unsigned frames)
{
    for (unsigned i = 0; i < frames; i++) {
        int32_t s = chip_sample(c) * (1 << CHIP_OUT_SHIFT);
        for (unsigned ch = 0; ch < 2; ch++) {
            int32_t v = lr[2 * i + ch] + s;      /* both operands are within 24 bits */
            lr[2 * i + ch] = v > 8388607 ? 8388607 : v < -8388608 ? -8388608 : v;
        }
    }
}

/* ---- App-layer glue ---------------------------------------------------- */
struct chip_lane { volatile uint32_t write, read; uint32_t data[CHIP_QUEUE]; };
static chip_t g_chip;
static struct chip_lane g_lane[2];
static volatile uint8_t g_reset;

static inline __attribute__((always_inline)) void barrier(void)
{
#ifdef FM1_HOST_TEST
    __asm__ volatile("" ::: "memory");
#else
    __asm__ volatile("csync" ::: "memory");
#endif
}

void chip_engine_init(int sample_rate)
{
    memset(g_lane, 0, sizeof g_lane);
    g_reset = 0;
    chip_init(&g_chip, sample_rate);
}

const chip_t *chip_engine_state(void) { return &g_chip; }

static void push(unsigned lane, uint32_t ev)
{
    struct chip_lane *q = &g_lane[lane];
    uint32_t w = q->write;
    if (w - q->read >= CHIP_QUEUE) { g_reset = 1; return; }   /* lost event: avoid stuck notes */
    q->data[w & (CHIP_QUEUE - 1u)] = ev;
    barrier();
    q->write = w + 1u;
}

int chip_engine_midi(unsigned lane, const uint8_t *m, size_t n)
{
    if (!m || !n || n > 3 || lane > 1) return 0;
    if (m[0] == 0xff) { push(lane, 0xffu); return 0; }
    if (m[0] < 0x80 || m[0] >= 0xf0 || (m[0] & 15u) != CHIP_MIDI_CHANNEL) return 0;
    push(lane, (uint32_t)m[0] | (uint32_t)(n > 1 ? m[1] : 0) << 8 | (uint32_t)(n > 2 ? m[2] : 0) << 16);
    return 1;
}

void chip_engine_panic(void) { g_reset = 1; }

static FM1_AUDIO_CODE void drain(chip_t *c)
{
    for (unsigned l = 0; l < 2; l++) {
        struct chip_lane *q = &g_lane[l];
        uint32_t r = q->read;
        while (r != q->write) {
            barrier();
            chip_midi_event(c, q->data[r & (CHIP_QUEUE - 1u)]);
            ++r;
        }
        q->read = r;
    }
    /* Applied after the queues so a panic or a lost event (overflow) always ends
     * silent, whatever note messages were still pending. */
    if (g_reset) { g_reset = 0; chip_all_off(c); }
}

FM1_AUDIO_CODE void chip_engine_render_add(int32_t *output, unsigned frames)
{
    chip_t *c = &g_chip;
    if (!output || !c->sr) return;
    drain(c);
    int busy = c->x1 || c->y1;
    for (int k = 0; k < CHIP_MAX_VOICES && !busy; k++) busy = c->v[k].active;
    if (!busy) return;                     /* idle: no work and nothing to add */
    chip_render_q23_add(c, output, frames);
}
