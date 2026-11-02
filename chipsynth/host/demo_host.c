/* Renders demo.wav (44.1 kHz stereo) by driving the engine exactly as the firmware will:
 * MIDI messages on the chip channel into the event queue, then 128-frame block renders. */
#include "chip_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR 44100
#define CH (0x00 | CHIP_MIDI_CHANNEL)
static int16_t *pcm;
static size_t frames_out;

static void send3(unsigned status, unsigned a, unsigned b) { uint8_t m[3] = { (uint8_t)status, (uint8_t)a, (uint8_t)b }; chip_engine_midi(0, m, 3); }
static void on(int n, int v)  { send3(0x90 | CH, (unsigned)n, (unsigned)v); }
static void off(int n)        { send3(0x80 | CH, (unsigned)n, 0); }
static void cc(int c, int v)  { send3(0xB0 | CH, (unsigned)c, (unsigned)v); }
static void program(int p)    { uint8_t m[2] = { (uint8_t)(0xC0 | CH), (uint8_t)p }; chip_engine_midi(0, m, 2); }

static void run(int ms)
{
    size_t left = (size_t)SR * (size_t)ms / 1000;
    while (left) {
        unsigned n = left > FM1_BLOCK_FRAMES ? FM1_BLOCK_FRAMES : (unsigned)left;
        int32_t block[FM1_BLOCK_WORDS];
        memset(block, 0, sizeof block);
        chip_engine_render_add(block, n);
        for (unsigned i = 0; i < n; i++) {
            pcm[2 * (frames_out + i)]     = (int16_t)(block[2 * i] >> 8);
            pcm[2 * (frames_out + i) + 1] = (int16_t)(block[2 * i + 1] >> 8);
        }
        frames_out += n;
        left -= n;
    }
}
static void note(int n, int ms, int gap) { on(n, 110); run(ms); off(n); run(gap); }
static void chord(int a, int b, int c, int ms, int gap) { on(a, 100); on(b, 100); on(c, 100); run(ms); off(a); off(b); off(c); run(gap); }

static void write_wav(const char *path)
{
    FILE *f = fopen(path, "wb");
    uint32_t data = (uint32_t)(frames_out * 4), riff = 36 + data, sr = SR, br = SR * 4, fmt = 16;
    uint16_t pcmfmt = 1, ch = 2, ba = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmt, 4, 1, f); fwrite(&pcmfmt, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    fwrite(pcm, 2, frames_out * 2, f);
    fclose(f);
}

int main(void)
{
    pcm = calloc((size_t)SR * 90 * 2, sizeof *pcm);
    chip_engine_init(SR);

    for (int p = 0; p < chip_num_presets; p++) {          /* 1. preset tour via Program Change */
        program(p);
        printf("preset %d: %s\n", p, chip_presets[p].name);
        note(p == 2 || p == 7 ? 36 : (p >= 8 ? 40 : 60), 450, 120);
    }
    run(300);

    cc(71, 60); cc(72, 12); program(1);                   /* 2. mono legato lead with portamento */
    int mel[] = { 69, 72, 76, 74, 72, 69, 67, 69 };
    for (int i = 0; i < 8; i++) { on(mel[i], 110); run(i == 7 ? 500 : 200); }
    off(mel[7]); run(400);
    cc(72, 0);

    cc(71, 0); program(4);                                /* 3. poly arp chords */
    chord(57, 60, 64, 600, 50); chord(53, 57, 60, 600, 50);
    chord(55, 59, 62, 600, 50); chord(57, 60, 64, 900, 300);

    cc(71, 100); program(3);                              /* 4. chip mode + noise on low notes */
    for (int bar = 0; bar < 2; bar++) {
        on(24, 127); chord(60, 64, 67, 150, 0);
        on(60, 90); run(150);
        chord(62, 65, 69, 150, 0); chord(64, 67, 72, 150, 0);
    }
    run(300);

    cc(71, 60); cc(70, 100); program(7);                  /* 5. Game Boy flavor, wave bass */
    int bass[] = { 33, 33, 40, 33, 36, 36, 43, 36 };
    for (int i = 0; i < 8; i++) { on(bass[i], 120); run(160); off(bass[i]); run(40); }
    run(300);

    write_wav("demo.wav");
    printf("wrote demo.wav: %.1f s, %d Hz stereo\n", frames_out / (double)SR, SR);
    free(pcm);
    return 0;
}
