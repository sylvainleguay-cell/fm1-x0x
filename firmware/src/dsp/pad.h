/* SPDX-License-Identifier: GPL-3.0-only */
/* PAD: a Juno-style polysynth with a chord generator, in X0X's fourth part (the 303 B's place).
 *
 * The part's step lane plays one note a step, as the 303's; PAD turns it into a four-note chord:
 *   CHORD  MIN7 (root, b3, 5, b7), MAJ7 (root, 3, 5, 7) or AUTO: the diatonic seventh chord of the note in
 *          the major key KEY (I maj7, ii m7, iii m7, IV maj7, V 7, vi m7, vii m7b5; a note outside the key: maj7)
 *   VOICE  CLOSE (the four inside an octave), OPEN (root, 5, 7, the 3rd an octave up), WIDE (the root an octave
 *          down, then 5, 3 + 12, 7 + 12)
 * HOLD (ON, the default): a chord rings on through the empty steps to the next note (the sequencer's: seq_t
 * hold_mask); OFF: it stops at half its step, as a 303's, unless the next step is a TIE.
 * A step's ACCENT plays it louder; a SLIDE into the next step changes the chord without restarting the envelopes
 * (and glides). Chords alternate between two banks of four voices, so the last one releases under the next.
 *
 * The voice: one DCO a voice, its saw and its pulse from the same ramp (WAVE: SAW, PULSE, BOTH), the pulse's width
 * swept by a slow LFO (PWM), a square sub-oscillator an octave down (SUB); a one-pole high-pass (HPF 0..3, the
 * Juno's switch); a 24 dB/oct resonant low-pass (two state-variable filters, the second resonant) with half
 * keyboard tracking and its envelope's sweep (ENV); one ADSR for the amplitude and the filter, as on the 106.
 * After the voices, the Juno's chorus (OFF, I, II, I+II): a short delay swept by a triangle, its two taps in
 * opposite phase to the left and the right: the part comes out in stereo. */
#pragma once
#include <stdint.h>
#include "x0x_param.h"

#define PAD_SR 44100.0f
#define PAD_NV 8                                 /* two banks of four */
#define PAD_DLY 256                              /* the chorus line: 5.8 ms (its taps reach 5.35), 16-bit */

enum {
    PAD_CUTOFF, PAD_RESO, PAD_ENVMOD, PAD_CHORUS,      /* page 1: TONE */
    PAD_CHORD, PAD_KEY, PAD_VOICING, PAD_WAVE,         /* page 2: CHORD */
    PAD_ATTACK, PAD_DECAY, PAD_SUSTAIN, PAD_RELEASE,   /* page 3: ENV */
    PAD_PWM, PAD_SUB, PAD_HPF, PAD_HOLD,               /* more (HOLD: the sequencer's, engine.c) */
    PAD_NPARAMS
};
enum { PAD_MIN7, PAD_MAJ7, PAD_AUTO };
enum { PAD_CLOSE, PAD_OPEN, PAD_WIDE };

typedef struct {
    float ph, inc, inc_to, sub;                  /* DCO ramp 0..1, its step (gliding to inc_to), the sub's ramp 0..2 */
    float s1a, s1b, s2a, s2b, hp;                /* the filters' states */
    float env;                                   /* ADSR level 0..1 */
    float vel;                                   /* this chord's level */
    float key;                                   /* the note, for the key tracking */
    uint8_t stage;                               /* 0 off, 1 attack, 2 decay / sustain, 3 release */
} pad_voice_t;

typedef struct {
    pad_voice_t v[PAD_NV];
    uint8_t pot[PAD_NPARAMS];
    int bank;                                    /* the bank the chord sounding is on (0 / 1) */
    int held;                                    /* a chord is held */
    float lfo, cho;                              /* PWM LFO and chorus LFO phases (0..1) */
    int16_t dly[PAD_DLY];                        /* x 8192 (the voices peak well inside +-4) */
    int w;
    /* derived (pad_set) */
    float cut_oct, reso_k, envmod, att, dec, sus, rel, pwm, sub, hpk, vol, cho_rate, cho_min, cho_span;
    int idle;                                    /* nothing sounding, chorus tail out: render writes zeros */
    int quiet;                                   /* samples since the last sound */
} pad_t;

void pad_init(pad_t *p);
void pad_note_on(pad_t *p, int note, int accent, int slide);
void pad_note_off(pad_t *p);
void pad_all_off(pad_t *p);
void pad_render(pad_t *p, float *out_l, float *out_r, int n);   /* writes (not adds) n samples a side */
int pad_chord(const pad_t *p, int note, int out[4]);             /* the notes of the chord on note (tests, UI) */
enum { PAD_Q_M7, PAD_Q_MAJ7, PAD_Q_7, PAD_Q_M7B5 };
int pad_quality(int chord, int key, int note);                   /* the chord's kind (PAD_Q_*) for CHORD, KEY pots */
extern const char *const PAD_Q_NAMES[4];                         /* "m7", "maj7", "7", "m7b5" */
int pad_nparams(void);
const x0x_param_t *pad_param(int i);
void pad_set(pad_t *p, int i, int value);
