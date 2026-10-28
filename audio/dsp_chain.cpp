// audio/dsp_chain.cpp — see dsp_chain.h for the design rationale.
//
// Chain (post-IR):  EQ (3-band) -> Dynamics (comp/limiter/sustain) -> Output level
//
// EQ + Dynamics boot DISABLED and Output level boots at the legacy IR_OUTPUT_SCALE,
// so a fresh boot sounds bit-identical to the pre-chain firmware until you enable a
// stage over UART. Coefficients recompute only when a param changes (dirty flag),
// and biquad/compressor state is preserved across recompute so you can sweep a knob
// while playing without clicks.

#include "audio/dsp_chain.h"
#include "audio/biquad.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

volatile bool g_dsp_bypass = false;

static float g_fs = 48000.0f;

// ---------------------------------------------------------------------------
// Stage 1: 3-band EQ — low shelf, mid peak (sweepable freq/Q), high shelf
// ---------------------------------------------------------------------------
enum { EQ_LO_F, EQ_LO_G, EQ_MID_F, EQ_MID_G, EQ_MID_Q, EQ_HI_F, EQ_HI_G, EQ_N };

static Param s_eq_params[EQ_N] = {   // voiced for warm open-G acoustic slide (K&K), dialed 2026-06-08
    { "lo_freq",   150.0f,   40.0f,   500.0f,   5.0f, "Hz" },   // +3 dB shelf: body/warmth
    { "lo_gain",     3.0f,  -15.0f,    15.0f,   0.5f, "dB" },
    { "mid_freq",  800.0f,  200.0f,  4000.0f,  10.0f, "Hz" },   // -3 dB scoop: de-honk, vocal mids
    { "mid_gain",   -3.0f,  -15.0f,    15.0f,   0.5f, "dB" },
    { "mid_q",       1.0f,    0.3f,     4.0f,   0.1f, "Q"  },
    { "hi_freq",  1500.0f, 1500.0f, 12000.0f,  50.0f, "Hz" },   // -2 dB shelf: roll off slide scrape
    { "hi_gain",    -2.0f,  -15.0f,    15.0f,   0.5f, "dB" },
};

typedef struct { Biquad lo, mid, hi; } EqState;
static EqState s_eq_state;

static void eq_recompute(Stage *s, float fs) {
    EqState *st = (EqState *)s->state;
    const Param *p = s->params;
    // Preserve filter memory across retune so live knob sweeps don't click.
    float lz1 = st->lo.z1,  lz2 = st->lo.z2;
    float mz1 = st->mid.z1, mz2 = st->mid.z2;
    float hz1 = st->hi.z1,  hz2 = st->hi.z2;
    biquad_lowshelf (&st->lo,  fs, p[EQ_LO_F].value,  0.707f,           p[EQ_LO_G].value);
    biquad_peaking  (&st->mid, fs, p[EQ_MID_F].value, p[EQ_MID_Q].value, p[EQ_MID_G].value);
    biquad_highshelf(&st->hi,  fs, p[EQ_HI_F].value,  0.707f,           p[EQ_HI_G].value);
    st->lo.z1 = lz1;  st->lo.z2 = lz2;
    st->mid.z1 = mz1; st->mid.z2 = mz2;
    st->hi.z1 = hz1;  st->hi.z2 = hz2;
}

static void eq_process(Stage *s, float *buf, int n) {
    EqState *st = (EqState *)s->state;
    for (int i = 0; i < n; i++) {
        float x = buf[i];
        x = biquad_process(&st->lo,  x);
        x = biquad_process(&st->mid, x);
        x = biquad_process(&st->hi,  x);
        buf[i] = x;
    }
}

// ---------------------------------------------------------------------------
// Stage 2: Dynamics — feed-forward peak compressor (comp / limiter / sustain)
//   sustain  = high ratio + long release
//   limiter  = very high ratio + fast attack + high threshold
// ---------------------------------------------------------------------------
enum { CP_THRESH, CP_RATIO, CP_ATT, CP_REL, CP_MAKEUP, CP_N };

static Param s_cp_params[CP_N] = {   // defaults tuned for acoustic bottleneck slide (K&K)
    { "thresh",  -16.0f, -60.0f,    0.0f,  1.0f, "dB" },
    { "ratio",     3.5f,   1.0f,   20.0f,  0.5f, ":1" },
    { "attack",   22.0f,   1.0f,  100.0f,  1.0f, "ms" },
    { "release", 300.0f,  10.0f, 1000.0f, 10.0f, "ms" },
    { "makeup",    6.0f, -12.0f,   24.0f,  0.5f, "dB" },
};

typedef struct {
    float env;          // detector envelope (preserved across recompute)
    float gain;         // current gain factor (held between control-rate updates)
    float att, rel;     // one-pole coeffs
    float thr_lin;      // threshold, linear
    float slope;        // 1/ratio - 1  (<= 0)
    float makeup_lin;
} CompState;
static CompState s_cp_state;
static float     s_comp_gr_peak = 1.0f;   // min comp gain since last meter read (1.0 = no reduction)
static float     s_comp_in_peak = 0.0f;   // peak |input| to the comp since last meter read

// Recompute the gain only every CP_CTRL samples. The envelope moves on ms time
// constants, so a ~6 kHz gain update (48k/8) is inaudible but avoids a per-sample
// powf — the per-sample transcendental was blowing the block budget (stutter).
#define CP_CTRL 8

static void comp_recompute(Stage *s, float fs) {
    CompState *st = (CompState *)s->state;
    const Param *p = s->params;
    st->att        = expf(-1.0f / (fs * p[CP_ATT].value * 0.001f));
    st->rel        = expf(-1.0f / (fs * p[CP_REL].value * 0.001f));
    st->thr_lin    = powf(10.0f, p[CP_THRESH].value / 20.0f);
    st->slope      = (1.0f / p[CP_RATIO].value) - 1.0f;
    st->makeup_lin = powf(10.0f, p[CP_MAKEUP].value / 20.0f);
}

static void comp_process(Stage *s, float *buf, int n) {
    CompState *st = (CompState *)s->state;
    float env  = st->env;
    float gain = st->gain;
    const float thr    = st->thr_lin;
    const float slope  = st->slope;
    const float makeup = st->makeup_lin;
    for (int i = 0; i < n; i++) {
        float x = buf[i];
        float a = fabsf(x);
        if (a > s_comp_in_peak) s_comp_in_peak = a;   // meter: peak input level
        // Peak detector: fast attack toward rising peaks, slow release. Per-sample.
        if (a > env) env = st->att * env + (1.0f - st->att) * a;
        else         env = st->rel * env + (1.0f - st->rel) * a;
        // Control-rate gain: (env/thr)^(1/ratio - 1) — one powf instead of the old
        // log10f + powf, and only every CP_CTRL samples.
        if ((i & (CP_CTRL - 1)) == 0) {
            gain = (env > thr && env > 1e-9f) ? powf(env / thr, slope) : 1.0f;
            if (gain < s_comp_gr_peak) s_comp_gr_peak = gain;   // meter: track peak reduction
        }
        buf[i] = x * gain * makeup;
    }
    st->env  = env;
    st->gain = gain;
}

// ---------------------------------------------------------------------------
// Stage 2.5: Neve-style transformer saturation (xfmr) — thickness and warmth
//   Added 2026-10-17 (brief: private/docs/daisy_slide_high_e_brief_2026-10-17.md, Tier 3).
//   A frequency-dependent asymmetric soft-clipper: a real transformer saturates more
//   at low frequencies, so a low-shelf PRE-emphasis pushes the lows/low-mids harder
//   into the nonlinearity, and a matching DE-emphasis undoes the shelf afterwards so
//   the static EQ balance is unchanged at low drive. The asymmetric bias adds EVEN
//   harmonics (transformer + class-A feel) on top of the tanh's odd ones.
//
//   Signal, per sample:
//     lo = lowshelf(x, 200 Hz, +tilt)           // push lows into the nonlinearity
//     s  = lo * drive
//     y  = tanh(s + bias) - tanh(bias)           // asym soft clip; -tanh(bias) kills static DC
//     y  = lowshelf(y, 200 Hz, -tilt) / gcomp    // undo the shelf; gcomp = small-signal gain
//     y  = highshelf(y, hf_freq, hf_gain)        // transformer HF softening (a gentle cut)
//     y  = dcblock(y, ~10 Hz)                    // mop up the dynamic DC the asymmetry leaves
//     out = mix * y + (1 - mix) * x              // level-matched wet/dry
//
//   gcomp = drive * (1 - tanh(bias)^2) is the clean small-signal gain of drive*tanh(.+bias),
//   so dividing by it keeps unity at low level for ANY drive/bias -- A/B tests judge tone, not
//   loudness. The +tilt/-tilt low shelves are magnitude-inverse, so a clean path is flat except
//   for the deliberate HF shelf.
//
//   ⚠ NO oversampling here. tanh harmonics fold back above Nyquist; at 48 kHz with drive <= ~6 dB
//   it is mostly inaudible (brief). drive is capped at 12 dB for that reason -- past ~10 dB add a
//   half-band 2x up/down around the nonlinearity and re-check CPU. ⚠ Default mix 0 = neutral.
// ---------------------------------------------------------------------------
enum { XF_DRIVE, XF_TILT, XF_BIAS, XF_HF_F, XF_HF_G, XF_MIX, XF_N };

static Param s_xf_params[XF_N] = {   // seeds = brief's tanglewood-slide-thick starting point
    { "drive",      6.0f,    0.0f,    12.0f,   0.5f, "dB" },   // into the nonlinearity (gain-compensated out)
    { "tilt",       4.0f,    0.0f,    12.0f,   0.5f, "dB" },   // low-shelf pre-emphasis: 0 = flat saturation
    { "bias",       0.1f,    0.0f,     0.5f,  0.02f, "x"  },   // asymmetry -> even harmonics; 0 = symmetric
    { "hf_freq",10000.0f, 2000.0f, 16000.0f, 100.0f, "Hz" },   // transformer HF softening corner
    { "hf_gain",   -1.5f,   -6.0f,     0.0f,   0.5f, "dB" },   // soft top (a cut)
    { "mix",        0.0f,    0.0f,     1.0f,  0.05f, "x"  },   // ⚠ 0 = neutral (stage does nothing)
};

typedef struct {
    Biquad pre, de, hf;     // pre-emphasis / de-emphasis low shelves + HF shelf
    float  drive_lin;       // 10^(drive_dB/20)
    float  bias, tanh_bias; // asymmetry and its static offset
    float  inv_gcomp;       // 1 / small-signal gain, for level compensation
    float  mix;
    float  dc_r;            // DC-blocker pole (~10 Hz)
    float  dc_x1, dc_y1;    // DC-blocker state
} XfmrState;
static XfmrState s_xf_state;

static void xfmr_recompute(Stage *s, float fs) {
    XfmrState *st = (XfmrState *)s->state;
    const Param *p = s->params;
    // Preserve filter + DC-blocker memory across retune so live knob sweeps don't click.
    float pz1 = st->pre.z1, pz2 = st->pre.z2;
    float dz1 = st->de.z1,  dz2 = st->de.z2;
    float hz1 = st->hf.z1,  hz2 = st->hf.z2;
    biquad_lowshelf (&st->pre, fs, 200.0f,            0.707f,  p[XF_TILT].value);
    biquad_lowshelf (&st->de,  fs, 200.0f,            0.707f, -p[XF_TILT].value);
    biquad_highshelf(&st->hf,  fs, p[XF_HF_F].value,  0.707f,  p[XF_HF_G].value);
    st->pre.z1 = pz1; st->pre.z2 = pz2;
    st->de.z1  = dz1; st->de.z2  = dz2;
    st->hf.z1  = hz1; st->hf.z2  = hz2;

    st->drive_lin = powf(10.0f, p[XF_DRIVE].value / 20.0f);
    st->bias      = p[XF_BIAS].value;
    st->tanh_bias = tanhf(st->bias);
    float gcomp   = st->drive_lin * (1.0f - st->tanh_bias * st->tanh_bias);
    st->inv_gcomp = (gcomp > 1e-6f) ? (1.0f / gcomp) : 1.0f;
    st->mix       = p[XF_MIX].value;
    st->dc_r      = 1.0f - (2.0f * 3.14159265358979f * 10.0f / fs);   // ~10 Hz one-pole DC blocker
}

static void xfmr_process(Stage *s, float *buf, int n) {
    XfmrState *st = (XfmrState *)s->state;
    const float mix = st->mix;
    if (mix <= 0.0f) return;                  // neutral: leave the dry signal untouched (and ~free)
    const float drive = st->drive_lin, bias = st->bias, tb = st->tanh_bias;
    const float invg = st->inv_gcomp, r = st->dc_r;
    float x1 = st->dc_x1, y1 = st->dc_y1;
    for (int i = 0; i < n; i++) {
        float dry = buf[i];
        float lo  = biquad_process(&st->pre, dry);
        float y   = tanhf(lo * drive + bias) - tb;         // asymmetric soft clip
        y         = biquad_process(&st->de, y) * invg;     // de-emphasis + level compensation
        y         = biquad_process(&st->hf, y);            // HF softening
        float o   = y - x1 + r * y1;                       // DC blocker
        x1 = y; y1 = o;
        buf[i] = mix * o + (1.0f - mix) * dry;             // level-matched wet/dry
    }
    st->dc_x1 = x1; st->dc_y1 = y1;
}

// ---------------------------------------------------------------------------
// Stage 3: Output level (replaces the fixed IR_OUTPUT_SCALE)
// ---------------------------------------------------------------------------
enum { OUT_LEVEL, OUT_N };

static Param s_out_params[OUT_N] = {
    { "level", 0.7f, 0.0f, 2.0f, 0.05f, "x" },   // 0.7 ≈ legacy IR_OUTPUT_SCALE; up to +6 dB
};

typedef struct { float lin; } OutState;
static OutState s_out_state;

static void out_recompute(Stage *s, float fs) {
    (void)fs;
    ((OutState *)s->state)->lin = s->params[OUT_LEVEL].value;
}

static void out_process(Stage *s, float *buf, int n) {
    float lin = ((OutState *)s->state)->lin;
    for (int i = 0; i < n; i++) buf[i] *= lin;
}

// ---------------------------------------------------------------------------
// Input trim — scales the (hot) IR output DOWN before EQ/comp, so the comp sees a
// sane level and nothing clips even comp-off. The IR adds ~25 dB on transients
// (peaks hit +12 dBFS); dial in.level on the meter so `comp in` peaks ~-3 dBFS.
// ---------------------------------------------------------------------------
enum { IN_LEVEL, IN_N };
static Param s_in_params[IN_N] = {
    // Max raised 2.0 -> 8.0 (+18 dB) on 2026-07-24. Was an attenuator-only range, sized when the
    // job was purely "tame the hot IR" with the analog chain making up level ahead of the ADC.
    // Two measurements inverted that: the ADC clips at 1.35 V pk (Test C) so `pga` must run at 0,
    // and gain before the ADC buys no SNR (SNR session Block 3: +18 dB pga -> +17.4 dB noise), so
    // the makeup belongs AFTER the converter where it's free. Restoring op-amp-era staging on the
    // JFET daughter needs x2.5 (garrison) to x5.0 (tanglewood-slide) -- both past the old cap.
    // Step 0.01 -> 0.05: at the new 3.0-4.5 working values that's ~0.1 dB/detent (it was 800
    // detents end-to-end at 0.01). Nothing downstream assumes <=1.0 -- comp + out.level and the
    // int32 clip in main.cpp bound the DAC -- but this stage can now push the comp hard, so dial
    // it on `meter` (comp-in peaks ~-3 dBFS), not by ear alone.
    { "level", 0.30f, 0.0f, 8.0f, 0.05f, "x" },   // dialed for K&K slide: comp-in peaks ~-3 dBFS
};
typedef struct { float lin; } InState;
static InState s_in_state;
static void in_recompute(Stage *s, float fs) {
    (void)fs;
    ((InState *)s->state)->lin = s->params[IN_LEVEL].value;
}
static void in_process(Stage *s, float *buf, int n) {
    float lin = ((InState *)s->state)->lin;
    for (int i = 0; i < n; i++) buf[i] *= lin;
}

// ---------------------------------------------------------------------------
// Chain wiring
// ---------------------------------------------------------------------------
// IR enable — a flag only, NOT a chain stage. The 2048-tap convolution runs cross-core
// in main.cpp BEFORE dsp_chain_process(), gated by dsp_chain_ir_enabled(). It's driven
// entirely by the host's IR SELECTION ("none" vs a real IR — the `ir <name|none>` command,
// preset, or encoder IR picker, via dsp_chain_set_ir_enabled), so it is deliberately kept
// OUT of s_chain: there's no "ir on/off" toggle in the stage menu / `dump` (that would be a
// redundant second control). Global bypass still disables it (see dsp_chain_ir_enabled).
static Stage s_ir   = { "ir",   true,  NULL,         0,     false, NULL, NULL, NULL };
static Stage s_in   = { "in",   true,  s_in_params,  IN_N,  true,  in_recompute,   in_process,   &s_in_state  };
static Stage s_eq   = { "eq",   true,  s_eq_params,  EQ_N,  true,  eq_recompute,   eq_process,   &s_eq_state  };
// ⚠ xfmr boots DISABLED (off/neutral); a preset enables it. Sits POST-EQ, PRE-comp: the EQ shapes the
// spectrum first, then the transformer thickens it, then the comp catches whatever that produced.
static Stage s_xfmr = { "xfmr", false, s_xf_params,  XF_N,  true,  xfmr_recompute, xfmr_process, &s_xf_state  };
static Stage s_comp = { "comp", true,  s_cp_params,  CP_N,  true,  comp_recompute, comp_process, &s_cp_state  };
static Stage s_out  = { "out",  true,  s_out_params, OUT_N, true,  out_recompute,  out_process,  &s_out_state };

// Audio stage chain (post-IR): IN trim -> EQ -> xfmr -> Dynamics -> Output. s_ir is intentionally
// excluded (it's a selection, not a toggleable stage) — see above.
static Stage *s_chain[] = { &s_in, &s_eq, &s_xfmr, &s_comp, &s_out };
#define N_STAGES ((int)(sizeof(s_chain) / sizeof(s_chain[0])))

int    dsp_chain_stage_count(void) { return N_STAGES; }
Stage *dsp_chain_stage(int i)      { return (i >= 0 && i < N_STAGES) ? s_chain[i] : NULL; }

// Effective IR state for es8388_test's foreground loop: on only if the IR stage is
// enabled AND global bypass is off.
bool dsp_chain_ir_enabled(void) { return s_ir.enabled && !g_dsp_bypass; }

// Host-driven IR stage enable (main.cpp sets this from the current IR selection:
// off when "none" is selected, on when a real IR is loaded).
void dsp_chain_set_ir_enabled(bool on) { s_ir.enabled = on; }

// Peak compressor gain reduction (dB, <= 0) since the last call; resets each call so
// a periodic reader gets "peak GR over the interval". 0 dB = not compressing.
float dsp_chain_comp_gr_db(void) {
    float g = s_comp_gr_peak;
    s_comp_gr_peak = 1.0f;
    if (g >= 1.0f) return 0.0f;
    if (g < 1e-6f) g = 1e-6f;
    return 20.0f * log10f(g);
}

// Peak input level to the compressor (dBFS) since the last call; resets each call.
// Lets you set comp.thresh relative to the actual signal hitting the stage.
float dsp_chain_comp_in_db(void) {
    float p = s_comp_in_peak;
    s_comp_in_peak = 0.0f;
    if (p < 1e-6f) return -120.0f;
    return 20.0f * log10f(p);
}

// ---------------------------------------------------------------------------
// Presets — IR ref + every stage's params/enables. `ir` is resolved by the host
// (main.cpp) to a convolver IR: a built-in name or an SD WAV filename.
// The active list defaults to s_builtin, but dsp_chain_install_presets() can point
// it at an SD-parsed table (see audio/tt_store).
// ---------------------------------------------------------------------------
static const Preset s_builtin[] = {
    // ⚠ BUILT-IN TABLE IS DEFAULT-ONLY (2026-10-18). The voiced presets -- tanglewood-slide,
    // garrison, slg110s, walden -- live ONLY on the SD card (sdcard_template/tonetrix/presets.txt) and
    // are NOT mirrored here: dsp_chain_install_presets() swaps THIS table out for the card's list at
    // boot, so there is nothing to keep in sync. This single entry is the NO-CARD FALLBACK and the
    // inheritance seed a partial SD preset borrows every omitted key from (dsp_chain_default_preset()).
    // The detailed 2026-07-24 bench restaging of the old built-in tanglewood/garrison is in git history
    // if it is ever wanted again.
    //
    // name              ir     in    eq     cmp    out   in     lo_f lo_g mid_f  mid_g  mid_q hi_f   hi_g  thr  ratio att rel  mkup out    pga
    // default -- "basically bypass": a TRANSPARENT passthrough. No IR, EQ off, comp OFF, in/out at
    // unity, so engaging it colours nothing. Three jobs:
    //   (1) the INHERITANCE SEED -- a partial SD preset inherits every key it omits from here, which is
    //       why the comp params below stay at sane values. ⚠ attack 1 ms especially: at pga 0 nothing
    //       limits transients before the DAC, so any partial preset that turns comp ON must inherit a
    //       fast attack;
    //   (2) the dry A/B reference you switch to in order to hear what an IR/EQ/comp preset is doing;
    //   (3) the power-on / no-card preset.
    // ⚠ comp_on used to be a light SAFETY LIMITER; it is now FALSE so "basically bypass" is literal --
    // the comp params remain ONLY as the seed, and out.level is unity (was 0.60). The pedal also boots in
    // g_dsp_bypass regardless, so boot is passthrough either way; this makes the ENGAGED default match it.
    { "default",          NULL,         true, false, false, true, 1.00f, 120,  0,  700,  0.0f,  1.0f, 3500,  0.0f, -18, 3.0f,  1, 250,  6,  1.00f,  0 },
};

#define N_BUILTIN ((int)(sizeof(s_builtin) / sizeof(s_builtin[0])))

static const Preset *s_presets = s_builtin;   // active list (built-in or SD-installed)
static int           s_n_presets = N_BUILTIN;

void dsp_chain_install_presets(const Preset *arr, int n) {
    if (arr && n > 0) { s_presets = arr;      s_n_presets = n; }
    else              { s_presets = s_builtin; s_n_presets = N_BUILTIN; }
}

const Preset *dsp_chain_default_preset(void) {
    for (int i = 0; i < N_BUILTIN; i++)
        if (strcmp(s_builtin[i].name, "default") == 0) return &s_builtin[i];
    return &s_builtin[0];
}

int         dsp_chain_preset_count(void)     { return s_n_presets; }
const char *dsp_chain_preset_name(int idx)   { return (idx >= 0 && idx < s_n_presets) ? s_presets[idx].name : ""; }
const char *dsp_chain_preset_ir(int idx) {
    const char *ir = (idx >= 0 && idx < s_n_presets) ? s_presets[idx].ir : NULL;
    return ir ? ir : "";
}
int dsp_chain_preset_pga(int idx) {
    return (idx >= 0 && idx < s_n_presets) ? s_presets[idx].pga : -1;   // <0 = leave PGA unchanged
}

int dsp_chain_find_preset(const char *name) {
    for (int i = 0; i < s_n_presets; i++)
        if (strcmp(s_presets[i].name, name) == 0) return i;
    return -1;
}

int dsp_chain_load_preset(int idx) {
    if (idx < 0 || idx >= s_n_presets) return -1;
    const Preset *p = &s_presets[idx];
    // Note: the IR stage enable is driven by the host's IR selection (none vs a real IR),
    // not set here — see main.cpp app_preset_load / the IR switch.
    s_in.enabled = p->in_on;  s_eq.enabled = p->eq_on;
    s_comp.enabled = p->comp_on;  s_out.enabled = p->out_on;
    s_in_params[IN_LEVEL].value  = p->in_level;
    s_eq_params[EQ_LO_F].value   = p->lo_f;  s_eq_params[EQ_LO_G].value  = p->lo_g;
    s_eq_params[EQ_MID_F].value  = p->mid_f; s_eq_params[EQ_MID_G].value = p->mid_g;
    s_eq_params[EQ_MID_Q].value  = p->mid_q;
    s_eq_params[EQ_HI_F].value   = p->hi_f;  s_eq_params[EQ_HI_G].value  = p->hi_g;
    s_cp_params[CP_THRESH].value = p->thr;   s_cp_params[CP_RATIO].value = p->ratio;
    s_cp_params[CP_ATT].value    = p->att;   s_cp_params[CP_REL].value   = p->rel;
    s_cp_params[CP_MAKEUP].value = p->mkup;
    s_out_params[OUT_LEVEL].value = p->out_level;
    // ⚠ xfmr: built-in presets zero-init these (xfmr_on=false, everything 0), and an SD preset that
    // omits the keys inherits those zeros from `default`. Only COPY when the preset actually specified
    // the stage -- otherwise the Param-array seeds (drive 6, hf 10 kHz, mix 0) stand, so the stage is
    // off but ready to dial from the menu, and we never write hf_freq=0 (a degenerate shelf). A preset
    // with "xfmr.on"/"xfmr:" sets real values here.
    s_xfmr.enabled = p->xfmr_on;
    if (p->xfmr_on || p->xf_mix > 0.f || p->xf_drive > 0.f) {
        s_xf_params[XF_DRIVE].value = p->xf_drive; s_xf_params[XF_TILT].value = p->xf_tilt;
        s_xf_params[XF_BIAS].value  = p->xf_bias;  s_xf_params[XF_HF_F].value = p->xf_hf_f;
        s_xf_params[XF_HF_G].value  = p->xf_hf_g;  s_xf_params[XF_MIX].value  = p->xf_mix;
    }
    s_in.dirty = s_eq.dirty = s_xfmr.dirty = s_comp.dirty = s_out.dirty = true;
    return 0;
}

void dsp_chain_init(float fs, float out_level_lin) {
    g_fs = fs;
    s_out_params[OUT_LEVEL].value = out_level_lin;
    s_cp_state.env  = 0.0f;
    s_cp_state.gain = 1.0f;
    for (int i = 0; i < N_STAGES; i++) {
        s_chain[i]->recompute(s_chain[i], fs);
        s_chain[i]->dirty = false;
    }
}

// ⚠ Gain applied ONLY in bypass, to level-match the A/B. See byp_level in dsp_chain.h.
static volatile float g_byp_level = 1.0f;

void dsp_chain_set_byp_level(float g) {
    if (g < 0.f) g = 0.f;
    if (g > 8.f) g = 8.f;
    g_byp_level = g;
}
float dsp_chain_byp_level(void) { return g_byp_level; }

void dsp_chain_process(float *buf, int n) {
    // ⚠ Bypass takes the dry path and, optionally, a make-up gain -- nothing else. Returning early
    // rather than falling through the stage loop keeps that literally true: no stage can be left
    // running by accident the way the OUT stage was until 2026-09-07.
    if (g_dsp_bypass) {
        const float g = g_byp_level;
        if (g != 1.0f)
            for (int i = 0; i < n; i++) buf[i] *= g;
        return;
    }
    for (int i = 0; i < N_STAGES; i++) {
        Stage *s = s_chain[i];
        bool on = s->enabled;
        // ⚠ BYPASS IS TRUE UNITY -- the OUT stage is bypassed too. It used to stay engaged
        // ("level stays"), which was exactly backwards for an A/B: a body IR ATTENUATES, so a
        // preset's out level exists largely to make that loss back up. Keeping it in bypass applied
        // the make-up WITHOUT the loss, leaving bypass LOUDER than engaged -- measured 2026-09-07
        // on the tanglewood-slide preset as in -68 / out -58 with the chain bypassed.
        //
        // Unity bypass makes three things true at once:
        //   - the A/B is level-matched by CONSTRUCTION whenever a preset's out level compensates
        //     its IR, which is what tuning that control is for;
        //   - a bypass measurement is the FRONT END, not the front end times a hidden gain, so an
        //     SNR figure taken here is publishable;
        //   - the docs stop lying -- CLAUDE.md's "boot is complete passthrough ... reflects the
        //     front end alone" was true at boot and false the moment a preset loaded.
        //
        // ⚠ If a preset's out level does NOT match its IR loss, bypass and engaged will differ --
        // but that is now a PRESET TUNING question, visible in `out.level` and adjustable, rather
        // than a hidden asymmetry in the bypass path.
        if (g_dsp_bypass) on = false;
        if (!on) continue;
        if (s->dirty) { s->recompute(s, g_fs); s->dirty = false; }
        s->process(s, buf, n);
    }
}

// ---------------------------------------------------------------------------
// UART command interface (Core 0 only). Same model the encoder/menu will drive.
// ---------------------------------------------------------------------------
static Stage *find_stage(const char *name) {
    for (int i = 0; i < N_STAGES; i++)
        if (strcmp(s_chain[i]->name, name) == 0) return s_chain[i];
    return NULL;
}

static Param *find_param(Stage *s, const char *name) {
    for (int i = 0; i < s->n_params; i++)
        if (strcmp(s->params[i].name, name) == 0) return &s->params[i];
    return NULL;
}

static void chain_dump(void) {
    printf("--- DSP chain (bypass=%s) ---\n", g_dsp_bypass ? "on" : "off");
    for (int i = 0; i < N_STAGES; i++) {
        Stage *s = s_chain[i];
        printf("[%s] %s\n", s->name, s->enabled ? "on" : "off");
        for (int j = 0; j < s->n_params; j++) {
            Param *p = &s->params[j];
            printf("    %s.%-8s = %8.3g %-3s  [%g..%g]\n",
                   s->name, p->name, (double)p->value, p->unit,
                   (double)p->vmin, (double)p->vmax);
        }
    }
}

static void chain_help(void) {
    printf("DSP commands:\n"
           "  <stage>.<param> <val>   set param   (e.g. eq.mid_gain 3.5)\n"
           "  <stage>.<param>         show param\n"
           "  <stage> on|off          enable/bypass stage (in, eq, comp, out)\n"
           "  preset [name]           list presets, or load one (switches IR too)\n"
           "  in.level <x>            pre-comp trim + post-ADC makeup, 0..8 (meter to ~-3 dBFS)\n"
           "  bypass on|off           raw ADC->DAC: only out.level applies (IR/EQ/comp/in.level all off)\n"
           "  ir [name|none]          select IR: a name (tanglewood/garrison/an SD .wav), none=off,\n"
           "                          on=re-engage; no arg shows current + lists options\n"
           "  tuner on|off            guitar tuner (UART needle; dry monitor)\n"
           "  meter on|off            live level meter: comp in / GR / output dBFS / ADC+DAC clip (~1/s)\n"
           "  dump                    app/codec settings (pga, ir, preset...) + all stages + params\n"
           "  stats                   block / timing counters\n"
           "  help                    this\n");
}

bool dsp_chain_command(const char *line) {
    char buf[64];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    // Trim trailing CR/LF/space and leading space.
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n' || buf[len - 1] == ' '))
        buf[--len] = '\0';
    char *p = buf;
    while (*p == ' ') p++;
    if (*p == '\0') return false;

    // Split off the argument at the first space.
    char *arg = strchr(p, ' ');
    if (arg) { *arg = '\0'; arg++; while (*arg == ' ') arg++; }

    if (strcmp(p, "help") == 0) { chain_help(); return true; }
    if (strcmp(p, "dump") == 0) { chain_dump(); return true; }
    if (strcmp(p, "bypass") == 0) {
        if (arg) { g_dsp_bypass = (strcmp(arg, "on") == 0); }
        printf("bypass=%s\n", g_dsp_bypass ? "on" : "off");
        return true;
    }

    char *dot = strchr(p, '.');
    if (dot) {
        *dot = '\0';
        const char *sname = p, *pname = dot + 1;
        Stage *s = find_stage(sname);
        if (!s) { printf("? no stage '%s'\n", sname); return true; }
        Param *pr = find_param(s, pname);
        if (!pr) { printf("? no param '%s.%s'\n", sname, pname); return true; }
        if (arg) {
            float v = strtof(arg, NULL);
            if (v < pr->vmin) v = pr->vmin;
            if (v > pr->vmax) v = pr->vmax;
            pr->value = v;
            s->dirty = true;
        }
        printf("%s.%s=%.3g %s\n", sname, pname, (double)pr->value, pr->unit);
        return true;
    }

    // Bare stage token -> enable/bypass. Accept ONLY on/off so a malformed command
    // (e.g. "out level 0.2" with a space instead of a dot, or a stray serial byte)
    // can't silently disable a stage.
    Stage *s = find_stage(p);
    if (!s) return false;
    if (arg) {
        if (strcmp(arg, "on") == 0)       s->enabled = true;
        else if (strcmp(arg, "off") == 0) s->enabled = false;
        else { printf("? '%s' takes on|off (got '%s') — to set a param use '%s.<param> <val>'\n",
                      p, arg, p); return true; }
    }
    printf("%s=%s\n", p, s->enabled ? "on" : "off");
    return true;
}