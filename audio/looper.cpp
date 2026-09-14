// audio/looper.cpp — see looper.h.
#include "audio/looper.h"
#include "dev/sdram.h"      // DSY_SDRAM_BSS
#include <string.h>
#include <math.h>

#define LEN_MAX   ((uint32_t)LOOPER_MAX_SECONDS * LOOPER_RATE)

// ⚠ SDRAM. 8 layers x 60 s x 48 kHz x float = 92 MB -- more than the 64 MB fitted, so layers are
// int16: 46 MB, and 16 bits is plenty for a bed that sits under a live guitar. Written with a
// clamp, read back through a 1/32768 scale.
static int16_t DSY_SDRAM_BSS s_layer[LOOPER_MAX_LAYERS][LEN_MAX];

static volatile looper_state_t s_state  = LOOPER_EMPTY;
static volatile int            s_layers = 0;        // committed layers
// ⚠ NO memset IN THE CALLBACK. Zeroing a 5.8 MB layer in SDRAM is tens of milliseconds -- a
// guaranteed glitch on the press that starts recording. Instead each layer remembers how far it
// was WRITTEN: a record pass writes every sample it covers, so the only stale bytes a reader could
// meet are past the end of an overdub that was cut short -- and those read as zero via s_written.
// An overdub records for one full loop from wherever it started, so it also tracks its start.
static uint32_t s_written[LOOPER_MAX_LAYERS];       // valid samples [0, written) for layers 0..n-1
static uint32_t s_od_start = 0;                     // overdub: position it began at
static uint32_t s_od_count = 0;                     // overdub: samples recorded so far (ends at s_len)
static bool     s_od_wrapped = false;
static volatile uint32_t       s_len    = 0;        // loop length, samples (0 until the first pass ends)
static volatile uint32_t       s_pos    = 0;        // play / record head
static volatile float          s_level  = 1.0f;

static float s_bpm  = 0.f;      // locked (set_tempo) or detected
static int   s_bars = 0;
static bool  s_bpm_locked = false;

// Requests from the foreground, consumed by the callback at a block boundary. One pending action
// is enough: the switches cannot outrun 750 blocks/s.
enum Req : uint8_t { REQ_NONE, REQ_PRESS, REQ_STOP, REQ_UNDO, REQ_RESET };
static volatile uint8_t s_req = REQ_NONE;

void looper_set_level(float g)
{
    if(g < 0.f) g = 0.f;
    if(g > 2.f) g = 2.f;
    s_level = g;
}

void looper_set_tempo(float bpm, int bars)
{
    s_bpm        = bpm > 0.f ? bpm : 0.f;
    s_bars       = bars > 0 ? bars : 0;
    s_bpm_locked = s_bpm > 0.f;
}

void looper_reset(void)  { s_req = REQ_RESET; }
void looper_press(void)  { s_req = REQ_PRESS; }
void looper_stop(void)   { s_req = REQ_STOP;  }
void looper_undo(void)   { s_req = REQ_UNDO;  }

looper_state_t looper_state(void)    { return s_state; }
int            looper_layers(void)   { return s_layers; }
uint32_t       looper_length(void)   { return s_len; }
uint32_t       looper_position(void) { return s_pos; }
float          looper_bpm(void)      { return s_bpm; }

// Length of one bar in samples at the locked bpm, 4/4.
static inline uint32_t BarSamples(void)
{
    return (uint32_t)(4.f * 60.f * (float)LOOPER_RATE / s_bpm + 0.5f);
}

// Close the FIRST pass at `raw` samples: apply the tempo rules, set s_len.
static void CloseFirstPass(uint32_t raw)
{
    uint32_t len = raw;
    if(s_bpm_locked && s_bars > 0)
        len = BarSamples() * (uint32_t)s_bars;                 // fixed length
    else if(s_bpm_locked)
    {
        const uint32_t bar = BarSamples();                     // snap to whole bars, at least one
        uint32_t bars = (raw + bar / 2) / bar;
        if(bars < 1) bars = 1;
        len = bar * bars;
    }
    else
    {
        // Free: the pass IS the length. Guess a bpm from it for the display: assume 4/4 and pick the
        // bar count whose tempo lands in 60..160, nearest 100 -- where most jams sit. Loops shorter
        // than ~1.5 s have no whole-bar reading in that range and show no bpm, which is honest.
        float best = 0.f, best_d = 1e9f;
        for(int bars = 1; bars <= 32; bars++)
        {
            // bars * 4 beats in raw/RATE seconds -> beats per minute
            const float bpm = (float)bars * 4.f * 60.f * (float)LOOPER_RATE / (float)raw;
            if(bpm < 60.f || bpm > 160.f) continue;
            const float d = fabsf(bpm - 100.f);
            if(d < best_d) { best_d = d; best = bpm; }
        }
        s_bpm = best;                                           // 0 if nothing fits (a very short slip)
    }
    if(len > LEN_MAX) len = LEN_MAX;
    if(len < 4800)    len = 4800;                              // 100 ms floor: a slip is not a loop
    // A locked length longer than what was recorded: the tail of the layer is silence already
    // (layers are zeroed at the start of a pass), so nothing to do.
    s_len = len;
}

static void Apply(uint8_t req)
{
    switch(req)
    {
        case REQ_RESET:
            s_state = LOOPER_EMPTY; s_layers = 0; s_len = 0; s_pos = 0;
            if(!s_bpm_locked) s_bpm = 0.f;
            break;

        case REQ_PRESS:
            switch(s_state)
            {
                case LOOPER_EMPTY:                              // first pass starts
                    s_pos = 0; s_state = LOOPER_RECORDING;
                    break;
                case LOOPER_RECORDING:                          // first pass ends: length set, play
                    s_written[0] = s_pos;                       // a locked length beyond this reads as 0
                    CloseFirstPass(s_pos);
                    s_layers = 1; s_pos = 0; s_state = LOOPER_PLAYING;
                    break;
                case LOOPER_PLAYING:                            // start an overdub layer
                    if(s_layers < LOOPER_MAX_LAYERS)
                    {
                        s_od_start = s_pos; s_od_count = 0; s_od_wrapped = false;
                        s_written[s_layers] = 0;                // nothing valid yet
                        s_state = LOOPER_OVERDUB;               // records from the CURRENT position
                    }
                    break;
                case LOOPER_OVERDUB:                            // end the overdub early: keep it
                    // Recorded [od_start, od_start+count) mod len. s_written expresses a prefix
                    // only, so: wrapped => the whole loop is valid; not wrapped and started at 0
                    // => the prefix [0, count); otherwise the unrecorded head would read stale --
                    // zero it NOW (bounded: at most od_start samples, and this is a rare path).
                    if(s_od_wrapped)            s_written[s_layers] = s_len;
                    else if(s_od_start == 0)    s_written[s_layers] = s_od_count;
                    else
                    {
                        memset(s_layer[s_layers], 0, (size_t)s_od_start * sizeof(int16_t));
                        s_written[s_layers] = s_od_start + s_od_count;
                        if(s_written[s_layers] > s_len) s_written[s_layers] = s_len;
                    }
                    s_layers++; s_state = LOOPER_PLAYING;
                    break;
                case LOOPER_STOPPED:                            // play from the top
                    s_pos = 0; s_state = LOOPER_PLAYING;
                    break;
            }
            break;

        case REQ_STOP:
            switch(s_state)
            {
                case LOOPER_RECORDING:  s_written[0] = s_pos; CloseFirstPass(s_pos); s_layers = 1; s_pos = 0; s_state = LOOPER_STOPPED; break;
                case LOOPER_OVERDUB:
                    if(s_od_wrapped)            s_written[s_layers] = s_len;
                    else if(s_od_start == 0)    s_written[s_layers] = s_od_count;
                    else { memset(s_layer[s_layers], 0, (size_t)s_od_start * sizeof(int16_t));
                           s_written[s_layers] = s_od_start + s_od_count;
                           if(s_written[s_layers] > s_len) s_written[s_layers] = s_len; }
                    s_layers++; s_pos = 0; s_state = LOOPER_STOPPED; break;
                case LOOPER_PLAYING:                           s_pos = 0; s_state = LOOPER_STOPPED; break;
                default: break;
            }
            break;

        case REQ_UNDO:
            switch(s_state)
            {
                case LOOPER_OVERDUB:                            // discard the pass in progress
                    s_state = LOOPER_PLAYING;
                    break;
                case LOOPER_RECORDING:                          // abandon the first pass
                    s_state = LOOPER_EMPTY; s_len = 0; s_pos = 0;
                    break;
                case LOOPER_PLAYING:
                case LOOPER_STOPPED:
                    if(s_layers > 0) s_layers--;
                    if(s_layers == 0) { s_state = LOOPER_EMPTY; s_len = 0; s_pos = 0; if(!s_bpm_locked) s_bpm = 0.f; }
                    break;
                default: break;
            }
            break;
        default: break;
    }
}

static inline int16_t Clamp16(float v)
{
    v *= 32767.f;
    if(v >  32767.f) v =  32767.f;
    if(v < -32768.f) v = -32768.f;
    return (int16_t)v;
}

void looper_process(float *buf, int n)
{
    const uint8_t req = s_req;
    if(req != REQ_NONE) { s_req = REQ_NONE; Apply(req); }

    const looper_state_t st = s_state;
    if(st == LOOPER_EMPTY || st == LOOPER_STOPPED)
        return;

    const float    lvl  = s_level;
    const int      nl   = s_layers;
    uint32_t       pos  = s_pos;
    const uint32_t len  = s_len;

    for(int i = 0; i < n; i++)
    {
        const float in = buf[i];
        if(st == LOOPER_RECORDING)
        {
            // First pass: write, hear the live input only. Runs until the press, or the cap.
            s_layer[0][pos] = Clamp16(in);
            if(++pos >= LEN_MAX) { s_pos = pos; Apply(REQ_PRESS); return; }   // cap reached: close
            continue;
        }
        // Playing (with or without an overdub in progress): sum the committed layers, reading
        // zero past each layer's written extent.
        float mix = 0.f;
        for(int l = 0; l < nl; l++)
            if(pos < s_written[l])
                mix += (float)s_layer[l][pos];
        mix *= (1.f / 32768.f) * lvl;
        if(st == LOOPER_OVERDUB)
        {
            s_layer[nl][pos] = Clamp16(in);
            s_od_count++;
            // Extent: an overdub covers [start, len) then [0, start) once it wraps. Until it wraps
            // only the tail is valid; after, the whole loop is. s_written can only express a
            // prefix, so before the wrap it stays 0 (the new layer is inaudible until committed
            // anyway) and becomes len on commit.
        }
        buf[i] = in + mix;
        if(++pos >= len)
        {
            pos = 0;
            if(st == LOOPER_OVERDUB)
                s_od_wrapped = true;
        }
        if(st == LOOPER_OVERDUB && s_od_count >= len)           // one full loop recorded: commit
        {
            s_written[nl] = len;
            s_pos = pos; s_layers = nl + 1; s_state = LOOPER_PLAYING;
            for(int j = i + 1; j < n; j++)                      // finish the block as PLAYING
            {
                float m2 = 0.f;
                for(int l = 0; l <= nl; l++)
                    if(pos < s_written[l]) m2 += (float)s_layer[l][pos];
                buf[j] = buf[j] + m2 * (1.f / 32768.f) * lvl;
                if(++pos >= len) pos = 0;
            }
            s_pos = pos;
            return;
        }
    }
    s_pos = pos;
}
