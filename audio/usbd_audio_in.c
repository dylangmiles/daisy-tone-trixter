// audio/usbd_audio_in.c — the USB audio-IN streaming logic (ring + push + iso-IN drain + alt).
//
// Increment 2: this is now the audio HALF of the composite Audio+CDC device. No class/descriptor here
// anymore — usbd_composite.c owns the USBD_ClassTypeDef and the config descriptor and calls the
// tt_audio_* ops below (tt_audio_stream.h). The streaming model is unchanged from Phase 1: the pedal
// is the clock master, so the iso IN endpoint (now EP 0x83) is ASYNCHRONOUS — ~48 samples/frame
// nudged ±1 to keep the ring centred, no feedback endpoint.
//
// ⚠ The ring stores 24-bit precision (int32) regardless of AUDIO_BITS; SendFrame packs 2 or 3 bytes
// per sample. So flipping AUDIO_BITS 16->24 changes only the packing here + the descriptor there.
// Channel order per frame: [MAIN, MAIN, DRY, WET].
#include "tt_audio_stream.h"
#include "usbd_core.h"
#include "usbd_ctlreq.h"
#include "stm32h7xx.h"      // __DMB
#include <math.h>

// ---- lock-free SPSC ring (producer = audio callback; consumer = OTG IN interrupt) -----------------
// int32 words, 24-bit-scaled samples. 8192 words = 32 KB in SDRAM (DTCM is full; see g_usb_wet).
#define RING_WORDS         8192U
#define RING_MASK          (RING_WORDS - 1U)
static int32_t  s_ring[RING_WORDS] __attribute__((section(".sdram_bss")));
static volatile uint32_t  s_wr, s_rd;

static volatile uint8_t   s_alt        = 0;
static volatile uint8_t   s_streaming  = 0;
static volatile uint32_t  s_frames     = 0;
static volatile uint32_t  s_underruns  = 0;
static volatile uint32_t  s_overruns   = 0;
static volatile uint32_t  s_isoincomp  = 0;   // iso IN tokens the host missed (dropped frames)
static volatile uint32_t  s_sof        = 0;   // USB SOFs seen while streaming (host frame clock)
static volatile uint32_t  s_hist[4]    = {0, 0, 0, 0};  // packet-size histogram since last read: [47,48,49,50]
static volatile uint32_t  s_fmin       = 0xFFFFFFFFU;   // ring fill min since last read (frames)
static volatile uint32_t  s_fmax       = 0;             // ring fill max since last read
static volatile uint32_t  s_recenter   = 0;   // times the ring was force-drained back to target

// Fill controller: a 64-sample block lands in the ring at once while the wire drains ~48/frame, so the
// INSTANTANEOUS fill rides a ~±32-frame sawtooth at the block rate. Controlling the fine 47/49 nudge on
// that sawtooth makes it hunt every frame (seen on the studio USB panel). So the fine decision runs on an
// EMA of the fill (τ ≈ 2^FILL_AVG_K frames) that smooths the sawtooth out; the fast safety drain + the
// re-center still use the RAW fill so a real backlog is caught immediately.
#define FILL_AVG_K  5                 // EMA shift: τ ≈ 32 frames ≈ 32 ms (sawtooth period is ~1.3 frames)
static int32_t  s_favg = 0;           // EMA accumulator; filtered fill = s_favg >> FILL_AVG_K (0 = unseeded)

static uint8_t  s_tx[AUDIO_MAX_PACKET] __attribute__((aligned(4), section(".sdram_bss")));

// ---- frame assembly + transmit (OTG interrupt) ----------------------------------------------------
static void SendFrame(USBD_HandleTypeDef *pdev)
{
    const uint32_t wr = s_wr;
    uint32_t fill = (wr - s_rd) / AUDIO_CHANNELS;   // whole frames available
    if(fill < s_fmin) s_fmin = fill;                // telemetry: fill range before any re-center
    if(fill > s_fmax) s_fmax = fill;

    // ⚠ KEEP THE RING NEAR TARGET so it can't fill and overflow under DSP load — the overflow-drop
    // (tt_audio_push dropping whole 64-sample blocks) was the crackle: `ov` climbed while `fill` rode
    // near full (1947/2048) with the chain engaged. A transient (a preset load blocks ~100s of ms,
    // the IR re-inits under the callback) pumps the ring up, and a weak ±1 drain can't claw it back.
    //   1) Hard re-center: if the ring ever runs far over, drop the oldest frames back to TARGET in
    //      one step. One small discontinuity on a rare transient beats continuous overflow drops.
    //   2) Strong proportional drain: send up to +2 samples/frame when backed up so the ring returns
    //      to TARGET in ~1 s. macOS tolerates the packet-size variation (the UAC2 test proved the
    //      host SRC is not the issue), so a firm controller is fine.
    const int32_t TARGET = 256;                 // ~5.3 ms
    if(fill > (uint32_t)(4 * TARGET))           // ~21 ms backlog -> recover now
    {
        s_rd = wr - (uint32_t)TARGET * AUDIO_CHANNELS;
        fill = (uint32_t)TARGET;
        s_recenter++;
        s_favg = (int32_t)TARGET << FILL_AVG_K; // re-seed the filter after a hard jump
    }
    // Update the fill EMA (seed on the first frame so it doesn't ramp from 0).
    if(s_favg == 0) s_favg = (int32_t)fill << FILL_AVG_K;
    else            s_favg += (int32_t)fill - (s_favg >> FILL_AVG_K);
    const int32_t fill_avg = s_favg >> FILL_AVG_K;

    // Two-rate controller. FAST (raw fill): a real backlog drains hard NOW — up to +2/frame — so a DSP-
    // load / IR-load pump (seen on the studio USB panel as fill climbing past target with the chain
    // engaged) is clawed back before it grows. FINE (filtered fill): the ±1 nudge that holds the ring
    // against the slow codec-vs-USB clock drift runs on the SMOOTHED fill, so it no longer hunts 47/49
    // on the per-block sawtooth — which was most of the packet-size variation macOS glitches on.
    const int32_t err_raw = (int32_t)fill - TARGET;
    const int32_t err     = fill_avg - TARGET;
    uint32_t samples = AUDIO_NOM_SAMPLES;
    if(err_raw > 192)   samples = AUDIO_NOM_SAMPLES + 2U;   // real backlog  -> drain hard (50), fast
    else if(err > 8)    samples = AUDIO_NOM_SAMPLES + 1U;   // drifting over -> ease drain (49)
    else if(err < -8)   samples = AUDIO_NOM_SAMPLES - 1U;   // drifting under-> ease fill  (47)
    if(samples > AUDIO_MAX_SAMPLES) samples = AUDIO_MAX_SAMPLES;
    if(samples >= 47U && samples <= 50U) s_hist[samples - 47U]++;   // telemetry: packet-size histogram

    const uint32_t want = samples * AUDIO_CHANNELS;       // int32 words wanted
    const uint32_t have = (want <= (wr - s_rd)) ? want : (wr - s_rd);
    if(have < want) s_underruns++;

    uint8_t *d  = s_tx;
    uint32_t rd = s_rd;
    for(uint32_t i = 0; i < want; i++)
    {
        int32_t v = (i < have) ? s_ring[rd++ & RING_MASK] : 0;   // pad on underrun
#if AUDIO_SUBFRAME == 3
        *d++ = (uint8_t)(v);        *d++ = (uint8_t)(v >> 8); *d++ = (uint8_t)(v >> 16);  // 24-bit LE
#else
        int16_t s = (int16_t)(v >> 8);                                                    // top 16 of 24
        *d++ = (uint8_t)(s);        *d++ = (uint8_t)(s >> 8);
#endif
    }
    s_rd = rd;
    s_frames++;
    USBD_LL_Transmit(pdev, AUDIO_IN_EP, s_tx, want * AUDIO_SUBFRAME);
}

// ---- producer: audio callback, once per block -----------------------------------------------------
static inline int32_t f2i24(float x)
{
    x *= 8388607.0f;
    if(x > 8388607.0f)       x = 8388607.0f;
    else if(x < -8388608.0f) x = -8388608.0f;
    return (int32_t)lrintf(x);
}

void usb_audio_push(const float *master, const float *dry, const float *wet, uint32_t n)
{
    if(!s_streaming) return;
    const uint32_t wr   = s_wr;
    const uint32_t fill = wr - s_rd;
    if((RING_WORDS - fill) < (n * AUDIO_CHANNELS)) { s_overruns++; return; }   // would overflow -> drop
    uint32_t w = wr;
    for(uint32_t i = 0; i < n; i++)
    {
        const int32_t m = f2i24(master[i]);
        s_ring[w++ & RING_MASK] = m;                   // ch1 MAIN
        s_ring[w++ & RING_MASK] = m;                   // ch2 MAIN (dup)
        s_ring[w++ & RING_MASK] = f2i24(dry[i]);       // ch3 DRY
        s_ring[w++ & RING_MASK] = f2i24(wet[i]);       // ch4 WET
    }
    __DMB();
    s_wr = w;
}

// ---- ops called by the composite class ------------------------------------------------------------
void tt_audio_init(void)
{
    s_alt = 0; s_streaming = 0; s_wr = s_rd = 0; s_favg = 0;
}

void tt_audio_deinit(USBD_HandleTypeDef *pdev)
{
    if(s_streaming) USBD_LL_CloseEP(pdev, AUDIO_IN_EP);
    s_alt = 0; s_streaming = 0;
}

static void StartStream(USBD_HandleTypeDef *pdev)
{
    USBD_LL_OpenEP(pdev, AUDIO_IN_EP, USBD_EP_TYPE_ISOC, AUDIO_MAX_PACKET);
    pdev->ep_in[AUDIO_IN_EP & 0x0FU].is_used = 1U;
    s_wr = s_rd = 0;
    s_favg = 0;                      // fresh fill filter each time recording (re)starts
    s_streaming = 1;
    SendFrame(pdev);                 // prime: first transmit starts the DataIn cadence
}

static void StopStream(USBD_HandleTypeDef *pdev)
{
    s_streaming = 0;
    USBD_LL_FlushEP(pdev, AUDIO_IN_EP);
    USBD_LL_CloseEP(pdev, AUDIO_IN_EP);
    pdev->ep_in[AUDIO_IN_EP & 0x0FU].is_used = 0U;
}

uint8_t tt_audio_setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
    switch(req->bmRequest & USB_REQ_TYPE_MASK)
    {
        case USB_REQ_TYPE_STANDARD:
            switch(req->bRequest)
            {
                case USB_REQ_GET_INTERFACE:
                {
                    uint8_t alt = s_alt;
                    USBD_CtlSendData(pdev, &alt, 1U);
                    break;
                }
                case USB_REQ_SET_INTERFACE:
                {
                    uint8_t alt = (uint8_t)(req->wValue & 0xFFU);
                    if(alt != s_alt)
                    {
                        if(alt == 1U)        StartStream(pdev);
                        else if(s_streaming) StopStream(pdev);
                        s_alt = alt;
                    }
                    break;
                }
                case USB_REQ_GET_STATUS:
                {
                    uint16_t st = 0U;
                    USBD_CtlSendData(pdev, (uint8_t *)&st, 2U);
                    break;
                }
                default:
                    USBD_CtlError(pdev, req);
                    return USBD_FAIL;
            }
            break;

        case USB_REQ_TYPE_CLASS:
        {
            // UAC2 Clock Source controls. macOS queries the sample rate + validity from the clock
            // entity (ID AUDIO_CLOCK_ID) before it will stream, and uses the reported rate to drive
            // its (smooth) async sample-rate converter — the whole reason for the UAC2 port.
            static uint8_t ctl[14];
            const uint8_t entity = (uint8_t)(req->wIndex >> 8);     // high byte of wIndex = entity ID
            const uint8_t cs     = (uint8_t)(req->wValue >> 8);     // high byte of wValue = control sel
            if((req->bmRequest & 0x80U) && entity == AUDIO_CLOCK_ID)
            {
                const uint32_t f = AUDIO_FREQ;
                if(cs == 0x01U && req->bRequest == 0x01U)            // SAM_FREQ, GET CUR -> u32 rate
                {
                    ctl[0] = (uint8_t)f; ctl[1] = (uint8_t)(f >> 8);
                    ctl[2] = (uint8_t)(f >> 16); ctl[3] = (uint8_t)(f >> 24);
                    USBD_CtlSendData(pdev, ctl, 4U);
                    return USBD_OK;
                }
                if(cs == 0x01U && req->bRequest == 0x02U)            // SAM_FREQ, GET RANGE
                {
                    ctl[0] = 0x01U; ctl[1] = 0x00U;                   // wNumSubRanges = 1
                    ctl[2] = (uint8_t)f; ctl[3] = (uint8_t)(f >> 8);  // MIN
                    ctl[4] = (uint8_t)(f >> 16); ctl[5] = (uint8_t)(f >> 24);
                    ctl[6] = ctl[2]; ctl[7] = ctl[3]; ctl[8] = ctl[4]; ctl[9] = ctl[5];  // MAX
                    ctl[10] = 0; ctl[11] = 0; ctl[12] = 0; ctl[13] = 0;                   // RES = 0
                    USBD_CtlSendData(pdev, ctl, 14U);
                    return USBD_OK;
                }
                if(cs == 0x02U && req->bRequest == 0x01U)            // CLOCK_VALID, GET CUR -> 1 = valid
                {
                    ctl[0] = 1U;
                    USBD_CtlSendData(pdev, ctl, 1U);
                    return USBD_OK;
                }
            }
            USBD_CtlError(pdev, req);
            return USBD_FAIL;
        }

        default:
            USBD_CtlError(pdev, req);
            return USBD_FAIL;
    }
    return USBD_OK;
}

void tt_audio_datain(USBD_HandleTypeDef *pdev)
{
    if(s_streaming) SendFrame(pdev);
}

void tt_audio_isoincomplete(USBD_HandleTypeDef *pdev)
{
    if(s_streaming)
    {
        s_isoincomp++;
        USBD_LL_FlushEP(pdev, AUDIO_IN_EP);
        SendFrame(pdev);
    }
}

// ---- status ---------------------------------------------------------------------------------------
uint8_t  tt_audio_streaming(void)  { return s_streaming; }
uint32_t tt_audio_frames(void)     { return s_frames; }
uint32_t tt_audio_underruns(void)  { return s_underruns; }
uint32_t tt_audio_overruns(void)   { return s_overruns; }
uint32_t tt_audio_isoincomp(void)  { return s_isoincomp; }
uint32_t tt_audio_recenter(void)   { return s_recenter; }
uint32_t tt_audio_sof_count(void)  { return s_sof; }
void     tt_audio_sof(void)        { if(s_streaming) s_sof++; }   // called from the composite SOF cb

// Snapshot + reset the per-interval telemetry (packet histogram, fill range). For the USB-health panel.
void tt_audio_hist_take(uint32_t out[4])
{
    out[0] = s_hist[0]; out[1] = s_hist[1]; out[2] = s_hist[2]; out[3] = s_hist[3];
    s_hist[0] = s_hist[1] = s_hist[2] = s_hist[3] = 0;
}
void tt_audio_fill_range_take(uint32_t *mn, uint32_t *mx)
{
    *mn = (s_fmin == 0xFFFFFFFFU) ? 0U : s_fmin;
    *mx = s_fmax;
    s_fmin = 0xFFFFFFFFU;
    s_fmax = 0;
}
uint32_t tt_audio_ring_fill(void)  { return (s_wr - s_rd) / AUDIO_CHANNELS; }
