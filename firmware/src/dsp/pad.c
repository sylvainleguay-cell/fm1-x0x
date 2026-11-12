/* SPDX-License-Identifier: GPL-3.0-only */
/* PAD: a Juno-style polysynth with a chord generator; see pad.h. */
#include "pad.h"
#include "fastmath.h"

#define CTL 32                                   /* samples between filter-coefficient updates */

static const char *const N_CHORD[] = {"MIN7", "MAJ7", "AUTO", "NOTE"};
static const char *const N_KEY[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const N_VOICING[] = {"CLOSE", "OPEN", "WIDE"};
static const char *const N_CHORUS[] = {"OFF", "I", "II", "I+II"};
static const char *const N_HOLD[] = {"OFF", "ON"};

static const x0x_param_t PARAMS[PAD_NPARAMS] = {
    {"Cutoff", 127, 70, 0},
    {"Reso", 127, 25, 0},
    {"Env", 127, 30, 0},
    {"Chorus", 3, 1, N_CHORUS},
    {"Chord", 3, PAD_MIN7, N_CHORD},
    {"Key", 11, 0, N_KEY},
    {"Voice", 2, PAD_OPEN, N_VOICING},
    {"Hold", 1, 1, N_HOLD},
    {"Saw", 127, 127, 0},
    {"Pulse", 127, 0, 0},
    {"PWM", 127, 40, 0},
    {"Sub", 127, 35, 0},
    {"Attack", 127, 50, 0},
    {"Decay", 127, 80, 0},
    {"Sustain", 127, 100, 0},
    {"Releas", 127, 85, 0},
};

int pad_nparams(void) { return PAD_NPARAMS; }
const x0x_param_t *pad_param(int i) { return i >= 0 && i < PAD_NPARAMS ? &PARAMS[i] : 0; }

/* the chord's intervals, by PAD_Q_*: m7, maj7, 7 (dominant), m7b5 (half-diminished) */
static const int8_t IV[4][4] = {{0, 3, 7, 10}, {0, 4, 7, 11}, {0, 4, 7, 10}, {0, 3, 6, 10}};
/* AUTO: the diatonic seventh of each degree of the major scale; -1: outside the key (maj7) */
static const int8_t DEG[12] = {PAD_Q_MAJ7, -1, PAD_Q_M7, -1, PAD_Q_M7, PAD_Q_MAJ7, -1, PAD_Q_7, -1, PAD_Q_M7, -1, PAD_Q_M7B5};
const char *const PAD_Q_NAMES[4] = {"m7", "maj7", "7", "m7b5"};

int pad_quality(int chord, int key, int note)
{
    int q;
    if (chord == PAD_NOTE)
        return -1;
    if (chord != PAD_AUTO)
        return chord == PAD_MAJ7 ? PAD_Q_MAJ7 : PAD_Q_M7;
    q = DEG[((note - key) % 12 + 12) % 12];
    return q < 0 ? PAD_Q_MAJ7 : q;
}

int pad_chord(const pad_t *p, int note, int out[4])
{
    int k, q = pad_quality(p->pot[PAD_CHORD], p->pot[PAD_KEY], note);
    const int8_t *iv;
    if (q < 0) {                                 /* NOTE: the note alone */
        out[0] = note < 12 ? 12 : note > 115 ? 115 : note;
        return 1;
    }
    iv = IV[q];
    switch (p->pot[PAD_VOICING]) {
    case PAD_OPEN: out[0] = note; out[1] = note + iv[2]; out[2] = note + iv[3]; out[3] = note + iv[1] + 12; break;
    case PAD_WIDE: out[0] = note - 12; out[1] = note + iv[2]; out[2] = note + iv[1] + 12; out[3] = note + iv[3] + 12; break;
    default:
        for (k = 0; k < 4; k++)
            out[k] = note + iv[k];
        break;
    }
    for (k = 0; k < 4; k++)
        out[k] = out[k] < 12 ? 12 : out[k] > 115 ? 115 : out[k];
    return 4;
}

static float note_inc(float n) { return 440.0f * fm_exp2f((n - 69.0f) * (1.0f / 12.0f)) * (1.0f / PAD_SR); }

/* a time pot (0..127) as a one-pole's per-sample coefficient: ~1 ms .. 8 s (ms_lo .. ms_lo * 2^oct) */
static float tcoef(int pot, float ms_lo, float oct)
{
    float ms = ms_lo * fm_exp2f((float)pot * oct / 127.0f);
    return 1.0f - fm_expf(-1000.0f / (ms * PAD_SR));
}

void pad_set(pad_t *p, int i, int value)
{
    const x0x_param_t *d = pad_param(i);
    float x;
    if (!d)
        return;
    value = value < 0 ? 0 : value > d->max ? d->max : value;
    p->pot[i] = (uint8_t)value;
    x = (float)value / 127.0f;
    switch (i) {
    case PAD_CUTOFF: p->cut_oct = x * 9.8f; break;                          /* 20 Hz .. ~18 kHz */
    case PAD_RESO: p->reso_k = 2.0f - 1.94f * x; break;                     /* the second filter's damping */
    case PAD_ENVMOD: p->envmod = x * 7.0f; break;                            /* up to 7 octaves of sweep */
    case PAD_ATTACK: p->att = tcoef(value, 1.0f, 11.5f); break;             /* 1 ms .. ~3 s (linear ramp) */
    case PAD_DECAY: p->dec = tcoef(value, 4.0f, 11.0f); break;              /* 4 ms .. ~8 s */
    case PAD_SUSTAIN: p->sus = x * x; break;
    case PAD_RELEASE: p->rel = tcoef(value, 4.0f, 11.0f); break;
    case PAD_PWM: p->pwm = x * 0.45f; break;
    case PAD_SUB: p->sub = x * 0.8f; break;
    case PAD_CHORUS: {                                                       /* Juno-60: I 0.51 Hz, II 0.86 Hz, I+II 9.75 Hz */
        static const float RATE[4] = {0.0f, 0.513f, 0.863f, 9.75f}, LO[4] = {0, 1.66f, 1.66f, 3.25f},
                           HI[4] = {0, 5.35f, 5.35f, 3.85f};
        p->cho_rate = RATE[value] / PAD_SR;
        p->cho_min = LO[value] * PAD_SR * 0.001f;
        p->cho_span = (HI[value] - LO[value]) * PAD_SR * 0.001f;
        break;
    }
    default: break;                                                          /* CHORD KEY VOICE WAVE: read as played */
    }
}

void pad_init(pad_t *p)
{
    int i;
    uint8_t *z = (uint8_t *)p;
    for (i = 0; i < (int)sizeof *p; i++)
        z[i] = 0;
    for (i = 0; i < PAD_NPARAMS; i++)
        pad_set(p, i, PARAMS[i].def);
    p->vol = 0.41f;                              /* (its level: the mixer's) */
    p->hpk = 1.0f - fm_expf(-6.2831853f * 90.0f / PAD_SR);   /* the HPF: ~90 Hz, the Juno's first position */
    p->idle = 1;
}

void pad_note_on(pad_t *p, int note, int accent, int slide)
{
    int c[4], k, b, n;
    float vel = accent ? 1.0f : 0.72f;
    n = pad_chord(p, note, c);
    if (n == 1)
        vel *= 1.8f;                             /* one voice: as loud as a chord of four */
    if (slide && p->held) {                     /* legato: the same voices glide to the new chord */
        b = p->bank * 4;
        for (k = 0; k < 4; k++) {
            if (k < n) {
                p->v[b + k].inc_to = note_inc((float)c[k]);
                p->v[b + k].key = (float)c[k];
            } else if (p->v[b + k].stage && p->v[b + k].stage < 3)
                p->v[b + k].stage = 3;           /* a chord sliding to a NOTE: the others let go */
        }
        return;
    }
    for (k = 0; k < 4; k++)                      /* the chord sounding: released */
        if (p->v[p->bank * 4 + k].stage && p->v[p->bank * 4 + k].stage < 3)
            p->v[p->bank * 4 + k].stage = 3;
    p->bank ^= 1;
    b = p->bank * 4;
    for (k = 0; k < n; k++) {
        pad_voice_t *v = &p->v[b + k];
        v->inc = v->inc_to = note_inc((float)c[k]);
        v->key = (float)c[k];
        v->vel = vel;
        v->stage = 1;                            /* from where it is: no click if it still rang */
        if (v->env < 1e-4f) {
            v->ph = (float)k * 0.25f;            /* spread phases: no four saws in step */
            v->sub = 0.0f;
        }
    }
    p->held = 1;
    p->idle = 0;
    p->quiet = 0;
}

void pad_note_off(pad_t *p)
{
    int k;
    for (k = 0; k < PAD_NV; k++)
        if (p->v[k].stage && p->v[k].stage < 3)
            p->v[k].stage = 3;
    p->held = 0;
}

void pad_all_off(pad_t *p)
{
    int k;
    for (k = 0; k < PAD_NV; k++) {
        p->v[k].stage = 0;
        p->v[k].env = 0.0f;
    }
    p->held = 0;
}

static inline int16_t pad_q(float x)            /* a sample into the chorus line */
{
    x *= 8192.0f;
    return (int16_t)(x > 32767.0f ? 32767.0f : x < -32768.0f ? -32768.0f : x);
}

static inline float blep(float t, float dt)     /* polyBLEP residual */
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

/* one voice, n samples, added into mono */
static void voice_render(pad_t *p, pad_voice_t *v, float *mono, int n, float pw)
{
    const float gs = (float)p->pot[PAD_SAW] * (0.5f / 127.0f), gp = (float)p->pot[PAD_PULSE] * (0.5f / 127.0f);
    const float sub = p->sub * 0.5f, hpk = p->hpk, att = p->att, dec = p->dec, sus = p->sus, rel = p->rel;
    const float k2 = p->reso_k, comp = 1.0f + (2.0f - k2) * 0.35f;
    float ph = v->ph, inc = v->inc, sb = v->sub, env = v->env, hp = v->hp;
    float s1a = v->s1a, s1b = v->s1b, s2a = v->s2a, s2b = v->s2b;
    int i = 0, stage = v->stage;
    while (i < n) {
        int m = n - i < CTL ? n - i : CTL, j;
        float oct = p->cut_oct + p->envmod * env + (v->key - 60.0f) * (0.5f / 12.0f), hz, g, a1, a2, a3, b1, b2, b3,
              amp0 = env, amp1;
        hz = 20.0f * fm_exp2f(fm_clampf(oct, 0.0f, 9.8f));
        g = fm_tanf(3.14159265f * hz / PAD_SR);
        a1 = 1.0f / (1.0f + g * (g + 1.41421356f));
        a2 = g * a1;
        a3 = g * a2;
        b1 = 1.0f / (1.0f + g * (g + k2));
        b2 = g * b1;
        b3 = g * b2;
        if (inc != v->inc_to) {                  /* slide: ~60 ms glide, a step a control block */
            inc += (v->inc_to - inc) * 0.4f;
            if (fm_fabsf(v->inc_to - inc) < 1e-9f)
                inc = v->inc_to;
        }
        for (j = 0; j < m; j++) {               /* the envelope, per sample */
            if (stage == 1) {
                env += att * 1.3f;
                if (env >= 1.0f) {
                    env = 1.0f;
                    stage = 2;
                }
            } else if (stage == 2)
                env += (sus - env) * dec;
            else if (stage == 3) {
                env -= env * rel;
                if (env < 1e-4f) {
                    env = 0.0f;
                    stage = 0;
                }
            }
        }
        amp1 = env;
        for (j = 0; j < m; j++, i++) {
            float x = 0.0f, t, y, v1, v2, v3, a = (amp0 + (amp1 - amp0) * (float)j / (float)m) * v->vel;
            if (gs != 0.0f)
                x += gs * ((2.0f * ph - 1.0f) - blep(ph, inc));
            if (gp != 0.0f) {
                t = ph + 1.0f - pw;
                t -= (float)(int)t;
                x += gp * ((ph < pw ? 1.0f : -1.0f) + blep(ph, inc) - blep(t, inc));
            }
            if (sub != 0.0f)
                x += sb < 1.0f ? sub : -sub;
            ph += inc;
            if (ph >= 1.0f) {
                ph -= 1.0f;
                sb += 1.0f;
                if (sb >= 2.0f)
                    sb -= 2.0f;
            }
            if (hpk != 0.0f) {
                hp += (x - hp) * hpk;
                x -= hp;
            }
            v3 = x - s1b;                        /* the flat state-variable low-pass */
            v1 = a1 * s1a + a2 * v3;
            v2 = s1b + a2 * s1a + a3 * v3;
            s1a = 2.0f * v1 - s1a;
            s1b = 2.0f * v2 - s1b;
            v3 = v2 - s2b;                       /* the resonant one */
            v1 = b1 * s2a + b2 * v3;
            y = s2b + b2 * s2a + b3 * v3;
            s2a = 2.0f * v1 - s2a;
            s2b = 2.0f * y - s2b;
            mono[i] += y * comp * a;
        }
        if (!stage)
            break;
    }
    v->ph = ph;
    v->inc = inc;
    v->sub = sb;
    v->env = env;
    v->hp = fm_flush(hp);
    v->s1a = fm_flush(s1a);
    v->s1b = fm_flush(s1b);
    v->s2a = fm_flush(s2a);
    v->s2b = fm_flush(s2b);
    v->stage = (uint8_t)stage;
}

void pad_render(pad_t *p, float *out_l, float *out_r, int n)
{
    int i, k, any = 0;
    float pw;
    if (p->idle) {
        for (i = 0; i < n; i++)
            out_l[i] = out_r[i] = 0.0f;
        return;
    }
    for (i = 0; i < n; i++)
        out_l[i] = 0.0f;
    {   /* the PWM LFO, ~0.6 Hz triangle, once a block */
        float tri = p->lfo < 0.5f ? 4.0f * p->lfo - 1.0f : 3.0f - 4.0f * p->lfo;
        pw = 0.5f + p->pwm * tri;
        p->lfo += 0.6f * (float)n / PAD_SR;
        if (p->lfo >= 1.0f)
            p->lfo -= 1.0f;
    }
    for (k = 0; k < PAD_NV; k++)
        if (p->v[k].stage) {
            voice_render(p, &p->v[k], out_l, n, pw);
            any = 1;
        }
    for (i = 0; i < n; i++)
        out_l[i] *= p->vol;
    if (p->cho_rate == 0.0f) {                   /* no chorus: the same on both sides */
        for (i = 0; i < n; i++) {
            p->dly[p->w] = pad_q(out_l[i]);
            p->w = (p->w + 1) & (PAD_DLY - 1);
            out_r[i] = out_l[i];
        }
    } else {
        float ph = p->cho, rate = p->cho_rate, lo = p->cho_min, span = p->cho_span;
        int w = p->w;
        for (i = 0; i < n; i++) {
            float x = out_l[i], tri = ph < 0.5f ? 2.0f * ph : 2.0f - 2.0f * ph, d, f, a, b;
            int r;
            p->dly[w] = pad_q(x);
            d = lo + span * tri;                 /* left tap */
            r = (int)d;
            f = d - (float)r;
            a = (float)p->dly[(w - r) & (PAD_DLY - 1)] * (1.0f / 8192.0f);
            b = (float)p->dly[(w - r - 1) & (PAD_DLY - 1)] * (1.0f / 8192.0f);
            out_l[i] = 0.7f * (x + a + (b - a) * f);
            d = lo + span * (1.0f - tri);        /* right tap: the opposite phase */
            r = (int)d;
            f = d - (float)r;
            a = (float)p->dly[(w - r) & (PAD_DLY - 1)] * (1.0f / 8192.0f);
            b = (float)p->dly[(w - r - 1) & (PAD_DLY - 1)] * (1.0f / 8192.0f);
            out_r[i] = 0.7f * (x + a + (b - a) * f);
            w = (w + 1) & (PAD_DLY - 1);
            ph += rate;
            if (ph >= 1.0f)
                ph -= 1.0f;
        }
        p->cho = ph;
        p->w = w;
    }
    if (any)
        p->quiet = 0;
    else if ((p->quiet += n) > PAD_DLY + 256) {  /* the chorus line has emptied: idle */
        p->idle = 1;
        for (i = 0; i < PAD_DLY; i++)
            p->dly[i] = 0;
    }
}
