// audio/tuner.cpp — see tuner.h. YIN (cumulative-mean-normalized difference) pitch
// detection on a decimated window. O(maxlag * W) ~ 260k float ops per estimate
// (~0.5-1 ms) thanks to the /4 decimation, so it's affordable in tuner mode.

#include "audio/tuner.h"

#include <math.h>
#include <string.h>

#define TUNER_DECIM    4                 // input fs / 4 (48k -> 12k); guitar f0 <= ~1.5 kHz
#define TUNER_WIN      1024              // decimated samples per window (~85 ms at 12 kHz)
#define TUNER_MAXLAG   200               // lowest detectable f0 = 12000/200 = 60 Hz
// ⚠ WAS (TUNER_WIN / 2) = 512, i.e. down to 23 Hz -- and that was the real cause of the
// subharmonic lock, not the noise gate. YIN's difference function keeps getting LOWER at MULTIPLES
// of the true period: d'(2T) and d'(3T) are routinely smaller than d'(T). So whenever no dip cleared
// the threshold, the global-minimum fallback below searched all the way out to 23 Hz and landed on
// 2x or 3x the period. Measured 2026-09-10 on an E2 (82.4 Hz): a stream of 41.2 Hz and 27.5 Hz
// readings, which are exactly E2/2 and E2/3.
//
// ⚠ Capping the search at 60 Hz makes those lags UNREACHABLE. For an 82 Hz string the true period is
// ~146 samples and 2x is 292 -- outside the range, so it cannot be chosen. That fixes the fault at
// source instead of filtering it downstream, which is all main.cpp's band-limit could do.
// Bonus: the O(W * maxlag) difference loop gets 2.5x cheaper.
#define TUNER_MINLAG   8                 // highest detectable f0 ~ 12000/8  = 1500 Hz
#define TUNER_THRESH   0.15f             // YIN absolute threshold
#define TUNER_MIN_RMS  0.0012f           // ~-58 dBFS default; below this = no pitch (just noise)
// ⚠ DAISY FORK. Upstream is 0.004f (~-48 dBFS). Lowered deliberately: the gate exists to hide noise,
// but it hides SIGNAL at the same threshold -- at -48 the detector stops partway down a decaying
// string, the display freezes, and you have to strike the note again to see the effect of a peg
// turn. Pitch drift during decay is a true property of the sound and is exactly what you use to pick
// a sweet spot to tune to.
//
// ⚠ Builder's call, 2026-09-10, and the principle is the point: if the room is noisy, that is a
// problem to solve AT SOURCE (enclosure shielding, ground), not to paper over by desensitising the
// instrument. Measured idle floor on a quiet morning is ~-68 dBFS, so this leaves ~10 dB.
//
// ⚠ Only safe because main.cpp band-limits results to 60-1400 Hz and requires note confirmation.
// Runtime-settable with `tunergate` -- raise it on a noisy night rather than editing this.
static float s_min_rms = TUNER_MIN_RMS;
void tuner_set_min_rms(float v) { s_min_rms = (v > 0.f) ? v : TUNER_MIN_RMS; }
float tuner_min_rms(void)       { return s_min_rms; }

static float       g_fs_dec  = 12000.0f;
static float       s_win[TUNER_WIN];
static int         s_fill    = 0;
static int         s_dec_cnt = 0;
static float       s_dec_acc = 0.0f;
static float       s_d[TUNER_MAXLAG + 1];   // YIN difference, then CMND in place
static TunerResult s_result;

static const char *NOTE_NAMES[12] =
    { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

void tuner_init(float fs) {
    g_fs_dec  = fs / (float)TUNER_DECIM;
    s_fill    = 0;
    s_dec_cnt = 0;
    s_dec_acc = 0.0f;
    memset(&s_result, 0, sizeof(s_result));
    s_result.name = "-";
}

static void tuner_no_pitch(void) {
    s_result.valid   = false;
    s_result.freq_hz = 0.0f;
    s_result.cents   = 0.0f;
    s_result.name    = "-";
}

static void tuner_estimate(void) {
    const int W      = TUNER_WIN / 2;   // integration window
    const int maxlag = TUNER_MAXLAG;

    // Signal gate — don't chase the noise floor between notes.
    float energy = 0.0f;
    for (int i = 0; i < TUNER_WIN; i++) energy += s_win[i] * s_win[i];
    if (sqrtf(energy / TUNER_WIN) < s_min_rms) { tuner_no_pitch(); return; }

    // YIN difference function d(tau).
    s_d[0] = 1.0f;
    for (int tau = 1; tau <= maxlag; tau++) {
        float sum = 0.0f;
        for (int j = 0; j < W; j++) {
            float diff = s_win[j] - s_win[j + tau];
            sum += diff * diff;
        }
        s_d[tau] = sum;
    }

    // Cumulative mean normalized difference d'(tau).
    float running = 0.0f;
    for (int tau = 1; tau <= maxlag; tau++) {
        running += s_d[tau];
        s_d[tau] = (running > 0.0f) ? s_d[tau] * (float)tau / running : 1.0f;
    }

    // First dip below the absolute threshold (descend to its local min).
    int tau_est = -1;
    for (int tau = TUNER_MINLAG; tau < maxlag; tau++) {
        if (s_d[tau] < TUNER_THRESH) {
            while (tau + 1 < maxlag && s_d[tau + 1] < s_d[tau]) tau++;
            tau_est = tau;
            break;
        }
    }
    if (tau_est < 0) {   // fallback: global minimum, accepted only if reasonably clear
        int best = TUNER_MINLAG; float bestv = s_d[best];
        for (int tau = TUNER_MINLAG + 1; tau < maxlag; tau++)
            if (s_d[tau] < bestv) { bestv = s_d[tau]; best = tau; }
        // ⚠ 0.45, was 0.30. The fallback can be trusted further now that subharmonic lags are out
        // of range -- its old danger was picking a multiple, not picking a weak dip. Loosening it
        // converts a great many "no pitch" frames into usable readings, which is what makes the
        // display track a decaying note instead of freezing on the attack.
        if (bestv < 0.45f) tau_est = best;
    }
    if (tau_est < 0) { tuner_no_pitch(); return; }

    // Parabolic interpolation around the dip for sub-sample period precision.
    float betterTau = (float)tau_est;
    if (tau_est > 0 && tau_est < maxlag) {
        float s0 = s_d[tau_est - 1], s1 = s_d[tau_est], s2 = s_d[tau_est + 1];
        float denom = 2.0f * (2.0f * s1 - s2 - s0);
        if (denom != 0.0f) betterTau = (float)tau_est + (s2 - s0) / denom;
    }

    float f0 = g_fs_dec / betterTau;

    // Map to nearest equal-tempered note + cents.
    float midi_f = 69.0f + 12.0f * log2f(f0 / 440.0f);
    int   midi   = (int)lroundf(midi_f);
    int   ni     = ((midi % 12) + 12) % 12;

    s_result.freq_hz = f0;
    s_result.cents   = 100.0f * (midi_f - (float)midi);
    s_result.midi    = midi;
    s_result.name    = NOTE_NAMES[ni];
    s_result.octave  = midi / 12 - 1;
    s_result.clarity = 1.0f - s_d[tau_est];
    s_result.valid   = true;
}

bool tuner_feed(const float *samples, int n) {
    bool ready = false;
    for (int i = 0; i < n; i++) {
        s_dec_acc += samples[i];
        if (++s_dec_cnt >= TUNER_DECIM) {
            s_win[s_fill++] = s_dec_acc / (float)TUNER_DECIM;   // box-average decimation
            s_dec_acc = 0.0f;
            s_dec_cnt = 0;
            if (s_fill >= TUNER_WIN) {
                tuner_estimate();
                s_fill = 0;
                ready  = true;
            }
        }
    }
    return ready;
}

TunerResult tuner_result(void) { return s_result; }