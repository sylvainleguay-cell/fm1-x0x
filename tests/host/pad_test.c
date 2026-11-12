/* SPDX-License-Identifier: GPL-3.0-only */
/* The PAD (dsp/pad.c): its chords (MIN7, MAJ7, AUTO in every key, the voicings), that it sounds, stays finite and
 * bounded at every pot's ends, falls silent (idle) after a release, and that its chorus makes it stereo. */
#include <stdio.h>
#include <math.h>
#include "pad.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static pad_t p;
static float l[256], r[256];

static float run(int blocks, float *corr)
{
    float pk = 0.0f;
    double sl = 0, sr = 0, slr = 0;
    int b, i;
    for (b = 0; b < blocks; b++) {
        pad_render(&p, l, r, 256);
        for (i = 0; i < 256; i++) {
            float a = fabsf(l[i]) > fabsf(r[i]) ? fabsf(l[i]) : fabsf(r[i]);
            if (!(a == a) || a > 1e6f)
                return -1.0f;
            pk = a > pk ? a : pk;
            sl += (double)l[i] * (double)l[i];
            sr += (double)r[i] * (double)r[i];
            slr += (double)l[i] * (double)r[i];
        }
    }
    if (corr)
        *corr = (float)(slr / sqrt(sl * sr + 1e-30));
    return pk;
}

int main(void)
{
    int c[4], k, key, n, i, v;
    float corr, pk;
    pad_init(&p);
    /* the chords */
    pad_set(&p, PAD_VOICING, PAD_CLOSE);
    pad_set(&p, PAD_CHORD, PAD_MIN7);
    pad_chord(&p, 57, c);
    CHECK(c[0] == 57 && c[1] == 60 && c[2] == 64 && c[3] == 67, "MIN7 on A3: %d %d %d %d", c[0], c[1], c[2], c[3]);
    pad_set(&p, PAD_CHORD, PAD_MAJ7);
    pad_chord(&p, 48, c);
    CHECK(c[0] == 48 && c[1] == 52 && c[2] == 55 && c[3] == 59, "MAJ7 on C3: %d %d %d %d", c[0], c[1], c[2], c[3]);
    pad_set(&p, PAD_CHORD, PAD_AUTO);
    for (key = 0; key < 12; key++) {              /* AUTO: the diatonic sevenths, in every key */
        static const int Q[7] = {PAD_Q_MAJ7, PAD_Q_M7, PAD_Q_M7, PAD_Q_MAJ7, PAD_Q_7, PAD_Q_M7, PAD_Q_M7B5};
        static const int DEG[7] = {0, 2, 4, 5, 7, 9, 11};
        pad_set(&p, PAD_KEY, key);
        for (k = 0; k < 7; k++)
            CHECK(pad_quality(PAD_AUTO, key, 48 + key + DEG[k]) == Q[k], "AUTO key %d degree %d", key, k + 1);
        CHECK(pad_quality(PAD_AUTO, key, 49 + key) == PAD_Q_MAJ7, "AUTO key %d: a note outside it", key);
    }
    pad_set(&p, PAD_KEY, 0);
    pad_chord(&p, 55, c);                         /* G7 in C */
    CHECK(c[0] == 55 && c[1] == 59 && c[2] == 62 && c[3] == 65, "AUTO G in C: %d %d %d %d", c[0], c[1], c[2], c[3]);
    pad_set(&p, PAD_CHORD, PAD_NOTE);            /* NOTE: no chord */
    CHECK(pad_chord(&p, 57, c) == 1 && c[0] == 57 && pad_quality(PAD_NOTE, 0, 57) < 0, "NOTE: the note alone");
    pad_set(&p, PAD_CHORD, PAD_AUTO);
    pad_set(&p, PAD_VOICING, PAD_OPEN);
    pad_chord(&p, 57, c);                         /* Am7 open: A E G C+12 */
    CHECK(c[0] == 57 && c[1] == 64 && c[2] == 67 && c[3] == 72, "OPEN Am7: %d %d %d %d", c[0], c[1], c[2], c[3]);
    pad_set(&p, PAD_VOICING, PAD_WIDE);
    pad_chord(&p, 57, c);
    CHECK(c[0] == 45 && c[1] == 64 && c[2] == 72 && c[3] == 79, "WIDE Am7: %d %d %d %d", c[0], c[1], c[2], c[3]);
    /* it sounds, in stereo with its chorus, and mono without */
    pad_init(&p);
    CHECK(run(4, 0) == 0.0f && p.idle, "silent before a note");
    pad_note_on(&p, 57, 0, 0);
    pk = run(100, &corr);
    CHECK(pk > 0.05f && pk < 1.5f, "a chord's peak %f", (double)pk);
    CHECK(corr < 0.9f, "chorus I: the sides differ (correlation %f)", (double)corr);
    pad_set(&p, PAD_CHORUS, 0);
    run(4, 0);
    pk = run(20, &corr);
    CHECK(corr > 0.999f, "chorus off: mono (correlation %f)", (double)corr);
    /* NOTE: one voice sounds; a chord sliding to a NOTE keeps one */
    pad_init(&p);
    pad_set(&p, PAD_CHORD, PAD_NOTE);
    pad_note_on(&p, 57, 0, 0);
    run(10, 0);
    for (n = 0, k = 0; k < PAD_NV; k++)
        n += p.v[k].stage != 0;
    CHECK(n == 1, "NOTE: %d voices sound", n);
    pad_set(&p, PAD_CHORD, PAD_MIN7);
    pad_note_on(&p, 57, 0, 0);
    pad_set(&p, PAD_CHORD, PAD_NOTE);
    pad_note_on(&p, 60, 0, 1);
    run(1, 0);
    for (n = 0, k = 0; k < 4; k++)
        n += p.v[p.bank * 4 + k].stage == 1 || p.v[p.bank * 4 + k].stage == 2;
    CHECK(n == 1, "a chord sliding to a NOTE: %d voices held", n);
    /* the OSC mixer: saw off and pulse off, sub off: silence; the pulse alone sounds */
    pad_init(&p);
    pad_set(&p, PAD_SAW, 0);
    pad_set(&p, PAD_SUB, 0);
    pad_note_on(&p, 57, 0, 0);
    pk = run(40, 0);
    CHECK(pk < 1e-3f, "SAW 0 PULSE 0 SUB 0: silent (%f)", (double)pk);
    pad_set(&p, PAD_PULSE, 127);
    pk = run(40, 0);
    CHECK(pk > 0.05f, "PULSE alone: %f", (double)pk);
    pad_note_off(&p);
    /* release: it goes idle */
    pad_note_off(&p);
    for (i = 0; i < 2000 && !p.idle; i++)
        run(1, 0);
    CHECK(p.idle, "idle after the release");
    /* every pot at its ends, chords sliding, accents: finite and bounded */
    for (v = 0; v < 2; v++) {
        pad_init(&p);
        for (k = 0; k < PAD_NPARAMS; k++)
            pad_set(&p, k, v ? pad_param(k)->max : 0);
        for (n = 0; n < 12; n++) {
            pad_note_on(&p, 24 + n * 7, n & 1, n % 3 == 2);
            pk = run(20, 0);
            CHECK(pk >= 0.0f && pk < 4.0f, "pots at %s, note %d: peak %f", v ? "max" : "min", n, (double)pk);
        }
    }
    printf(fails ? "pad: %d failures\n" : "pad: all ok\n", fails);
    return fails != 0;
}
