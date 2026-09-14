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
// Onset tracking during the first pass, for the free-loop end snap: the sample index of the most
// recent attack (envelope crossing the threshold from below). A press within LOOPER_LATE_MS after
// it ends the loop THERE -- the common "late foot" case -- instead of at the press.
static uint32_t s_last_onset = 0;
static uint32_t s_prev_onset = 0;                   // the one before it: their gap is the beat
static float    s_env        = 0.f;                 // input envelope, fast attack / slow release
static bool     s_above      = false;
// ⚠ THE END PRESS SCHEDULES THE END, IT DOES NOT CUT. "That was the last beat" -- the loop has to run
// one more beat so the last note gets its length before beat one comes round. Recording continues
// to s_end_at (0 = not scheduled), then the pass closes there.
static uint32_t s_end_at     = 0;
// Metronome for bpm-locked songs: a short decaying tone on every beat (accented on the bar) from
// the moment the loop is ARMED until it plays. On the OUTPUT only, so it cannot trigger the arm.
static uint32_t s_click_pos  = 0;                   // samples into the current beat
static uint32_t s_click_beat = 0;                   // beat index (0 = downbeat)
static bool     s_click_on   = false;
static float    s_click_amp  = 0.f, s_click_ph = 0.f;
static looper_state_t s_before_press = LOOPER_EMPTY;   // for looper_unpress()
static uint32_t s_od_start = 0;                     // overdub: position it began at
static uint32_t s_od_count = 0;                     // overdub: samples recorded so far (ends at s_len)
static bool     s_od_wrapped = false;
static volatile uint32_t       s_len    = 0;        // loop length, samples (0 until the first pass ends)
static volatile uint32_t       s_pos    = 0;        // play / record head
static volatile float          s_level  = 1.0f;

static float s_bpm  = 0.f;      // locked (set_tempo) or detected
static int   s_bars = 0;
static bool  s_bpm_locked = false;

// Requests from the foreground, consumed by the callback at a block boundary, in order. A small
// FIFO rather than one slot: "unpress then undo" is two requests from one foreground iteration.
enum Req : uint8_t { REQ_NONE, REQ_PRESS, REQ_STOP, REQ_UNDO, REQ_RESET, REQ_UNPRESS };
static volatile uint8_t s_reqq[8];
static volatile uint8_t s_rq_w = 0, s_rq_r = 0;
static inline void Post(uint8_t r)
{
    const uint8_t nw = (uint8_t)((s_rq_w + 1) & 7);
    if(nw != s_rq_r) { s_reqq[s_rq_w] = r; s_rq_w = nw; }
}

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

void looper_reset(void)   { Post(REQ_RESET); }
void looper_press(void)   { Post(REQ_PRESS); }
void looper_unpress(void) { Post(REQ_UNPRESS); }
void looper_stop(void)    { Post(REQ_STOP); }
void looper_undo(void)    { Post(REQ_UNDO); }

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

static inline uint32_t BeatSamples(void)
{
    return (uint32_t)(60.f * (float)LOOPER_RATE / s_bpm + 0.5f);
}

// The end press at `raw` samples: where does the loop END? Never before `raw` (the last note needs
// its length), so recording runs on to the returned point.
static uint32_t ScheduleEnd(uint32_t raw)
{
    uint32_t end;
    if(s_bpm_locked && s_bars > 0)
        end = BarSamples() * (uint32_t)s_bars;                  // fixed length, wherever the press was
    else if(s_bpm_locked)
    {
        const uint32_t bar = BarSamples();                      // snap UP to the bar line: the press
        end = ((raw + bar - 1) / bar) * bar;                    // is "somewhere in the last bar"
        if(end == 0) end = bar;
    }
    else if(s_prev_onset > 0 && s_last_onset > s_prev_onset)
    {
        // Free: the gap between the last two attacks is the beat. "That was the last beat" =
        // the loop ends one beat after the last attack.
        const uint32_t beat = s_last_onset - s_prev_onset;
        end = s_last_onset + beat;
        if(end < raw) end = raw;                                // never cut what has been played
    }
    else
        end = raw;                                              // one attack or none: end at the press
    if(end > LEN_MAX) end = LEN_MAX;
    if(end < 4800)    end = 4800;
    return end;
}

// The pass has reached its scheduled end (or the cap): set s_len, guess a tempo if free.
static void CloseFirstPass(uint32_t len)
{
    if(!s_bpm_locked)
    {
        // Guess a bpm for the display from the loop length: 4/4, bar count that lands 60..160 nearest
        // 100. If we know the beat from the onsets, prefer that.
        if(s_prev_onset > 0 && s_last_onset > s_prev_onset)
            s_bpm = 60.f * (float)LOOPER_RATE / (float)(s_last_onset - s_prev_onset);
        else
        {
            float best = 0.f, best_d = 1e9f;
            for(int bars = 1; bars <= 32; bars++)
            {
                const float bpm = (float)bars * 4.f * 60.f * (float)LOOPER_RATE / (float)len;
                if(bpm < 60.f || bpm > 160.f) continue;
                const float d = fabsf(bpm - 100.f);
                if(d < best_d) { best_d = d; best = bpm; }
            }
            s_bpm = best;
        }
    }
    s_len = len;
}

static void Apply(uint8_t req)
{
    switch(req)
    {
        case REQ_RESET:
            s_state = LOOPER_EMPTY; s_layers = 0; s_len = 0; s_pos = 0; s_click_on = false; s_end_at = 0;
            if(!s_bpm_locked) s_bpm = 0.f;
            break;

        case REQ_UNPRESS:
            // The hold that follows a press: revert what the press did, so the undo that comes
            // next acts on the state the player was IN when the foot landed.
            switch(s_state)
            {
                case LOOPER_ARMED:     if(s_before_press == LOOPER_EMPTY)   { s_state = LOOPER_EMPTY; } break;
                case LOOPER_OD_ARMED:  if(s_before_press == LOOPER_PLAYING) { s_state = LOOPER_PLAYING; } break;
                case LOOPER_OVERDUB:                                    // press armed it and a note started it since
                    if(s_before_press == LOOPER_PLAYING) { s_state = LOOPER_PLAYING; }   // discard the pass
                    break;
                case LOOPER_PLAYING:
                    if(s_before_press == LOOPER_OVERDUB && s_layers > 0)   // press committed early: un-commit
                    { s_layers--; s_od_start = 0; s_od_count = s_len; s_od_wrapped = true; s_state = LOOPER_OVERDUB; }
                    else if(s_before_press == LOOPER_STOPPED)              // press restarted: stop again
                    { s_pos = 0; s_state = LOOPER_STOPPED; }
                    break;
                case LOOPER_RECORDING: if(s_before_press == LOOPER_RECORDING) s_end_at = 0; break;   // un-schedule
                default: break;
            }
            break;

        case REQ_PRESS:
            s_before_press = s_state;
            switch(s_state)
            {
                case LOOPER_EMPTY:                              // ARM: recording starts on the first note
                    s_pos = 0; s_env = 0.f; s_above = false; s_last_onset = 0; s_prev_onset = 0; s_end_at = 0;
                    s_state = LOOPER_ARMED;
                    if(s_bpm_locked) { s_click_on = true; s_click_pos = 0; s_click_beat = 0; s_click_amp = 0.f; }
                    break;
                case LOOPER_ARMED:                              // pressed again before playing: back to empty
                    s_state = LOOPER_EMPTY; s_click_on = false;
                    break;
                case LOOPER_RECORDING:                          // "that was the last beat": schedule the end
                    if(s_end_at == 0)
                        s_end_at = ScheduleEnd(s_pos);
                    // (a second press while waiting is ignored; the pass closes at s_end_at)
                    break;
                case LOOPER_PLAYING:                            // ARM an overdub: starts on the first note
                    if(s_layers < LOOPER_MAX_LAYERS)
                    {
                        s_written[s_layers] = 0;                // nothing valid yet
                        s_state = LOOPER_OD_ARMED;
                    }
                    break;
                case LOOPER_OD_ARMED:                           // pressed again before playing: cancel
                    s_state = LOOPER_PLAYING;
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
                case LOOPER_RECORDING:  s_written[0] = s_pos; CloseFirstPass(s_pos); s_layers = 1; s_pos = 0; s_state = LOOPER_STOPPED; s_click_on = false; break;
                case LOOPER_OVERDUB:
                    if(s_od_wrapped)            s_written[s_layers] = s_len;
                    else if(s_od_start == 0)    s_written[s_layers] = s_od_count;
                    else { memset(s_layer[s_layers], 0, (size_t)s_od_start * sizeof(int16_t));
                           s_written[s_layers] = s_od_start + s_od_count;
                           if(s_written[s_layers] > s_len) s_written[s_layers] = s_len; }
                    s_layers++; s_pos = 0; s_state = LOOPER_STOPPED; break;
                case LOOPER_OD_ARMED:
                case LOOPER_PLAYING:                           s_pos = 0; s_state = LOOPER_STOPPED; break;
                default: break;
            }
            break;

        case REQ_UNDO:
            switch(s_state)
            {
                case LOOPER_OD_ARMED:                           // nothing recorded yet: just disarm
                case LOOPER_OVERDUB:                            // discard the pass in progress
                    s_state = LOOPER_PLAYING;
                    break;
                case LOOPER_ARMED:
                case LOOPER_RECORDING:                          // abandon the first pass
                    s_state = LOOPER_EMPTY; s_len = 0; s_pos = 0; s_click_on = false;
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

// Metronome (bpm-locked, armed or first pass): a 1.5 kHz / 1 kHz tick, ~15 ms, accented on the
// downbeat. Added to the OUTPUT after the arm check, so it can never arm the loop itself.
static void Click(float *buf, int n)
{
    if(!s_click_on)
        return;
    const uint32_t beat = BeatSamples();
    for(int i = 0; i < n; i++)
    {
        if(s_click_pos == 0)
        {
            s_click_amp = (s_click_beat == 0) ? 0.5f : 0.3f;
            s_click_ph  = 0.f;
        }
        if(s_click_amp > 0.001f)
        {
            const float f = (s_click_beat == 0) ? 1500.f : 1000.f;
            buf[i] += s_click_amp * sinf(s_click_ph);
            s_click_ph  += 6.2831853f * f / (float)LOOPER_RATE;
            s_click_amp *= 0.9985f;                             // ~15 ms decay
        }
        if(++s_click_pos >= beat) { s_click_pos = 0; s_click_beat = (s_click_beat + 1) & 3; }
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
    while(s_rq_r != s_rq_w)
    {
        const uint8_t req = s_reqq[s_rq_r];
        s_rq_r = (uint8_t)((s_rq_r + 1) & 7);
        Apply(req);
    }

    looper_state_t st = s_state;

    if(st == LOOPER_EMPTY || st == LOOPER_STOPPED)
        return;

    // Armed: watch the input, start recording on the first sample over the threshold -- from THAT
    // sample, so the attack is in the loop. Nothing is mixed in; the player hears the live input.
    if(st == LOOPER_ARMED)
    {
        int start = -1;
        for(int i = 0; i < n; i++)
            if(fabsf(buf[i]) >= LOOPER_ARM_THRESHOLD) { start = i; break; }
        if(start >= 0)
        {
            s_pos = 0; s_state = LOOPER_RECORDING; st = LOOPER_RECORDING;
            s_env = 1.f; s_above = true; s_last_onset = 0; s_prev_onset = 0;   // the first note IS onset 0
            for(int i = start; i < n; i++)
                s_layer[0][s_pos++] = Clamp16(buf[i]);
            // With a click running, align the click to the first note: it started on beat 1.
            if(s_click_on) { s_click_pos = (uint32_t)(n - start) % BeatSamples(); s_click_beat = 0; }
        }
        Click(buf, n);
        return;
    }

    const float    lvl  = s_level;
    const int      nl   = s_layers;
    uint32_t       pos  = s_pos;
    const uint32_t len  = s_len;

    for(int i = 0; i < n; i++)
    {
        const float in = buf[i];
        if(st == LOOPER_RECORDING)
        {
            // First pass: write, hear the live input only. Runs until the scheduled end, or the cap.
            s_layer[0][pos] = Clamp16(in);
            // Onset tracker: envelope with ~1 ms attack / ~80 ms release; an "onset" is the envelope
            // crossing the threshold from below. The last two set the beat for a free loop.
            const float a = fabsf(in);
            s_env = (a > s_env) ? s_env + (a - s_env) * 0.02f : s_env + (a - s_env) * 0.00026f;
            if(!s_above && s_env >= LOOPER_ARM_THRESHOLD) { s_above = true; s_prev_onset = s_last_onset; s_last_onset = pos; }
            else if(s_above && s_env < LOOPER_ARM_THRESHOLD * 0.5f) s_above = false;
            pos++;
            const bool at_end = (s_end_at != 0 && pos >= s_end_at) || pos >= LEN_MAX;
            if(at_end)
            {
                s_written[0] = pos;
                CloseFirstPass(pos);
                s_layers = 1; s_pos = 0; s_state = LOOPER_PLAYING; s_click_on = false; s_end_at = 0;
                // The rest of this block plays the new loop from its top.
                uint32_t p2 = 0;
                for(int j = i + 1; j < n; j++)
                {
                    buf[j] += (p2 < s_written[0] ? (float)s_layer[0][p2] : 0.f) * (1.f / 32768.f) * lvl;
                    if(++p2 >= s_len) p2 = 0;
                }
                s_pos = p2;
                return;
            }
            continue;
        }
        // Overdub armed: the moment the input crosses the threshold, start the pass from THIS sample.
        if(st == LOOPER_OD_ARMED && fabsf(in) >= LOOPER_ARM_THRESHOLD)
        {
            s_od_start = pos; s_od_count = 0; s_od_wrapped = false;
            s_state = LOOPER_OVERDUB; st = LOOPER_OVERDUB;
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
    if(st == LOOPER_RECORDING)
        Click(buf, n);
}
