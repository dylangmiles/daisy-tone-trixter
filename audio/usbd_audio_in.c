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

static uint8_t  s_tx[AUDIO_MAX_PACKET] __attribute__((aligned(4), section(".sdram_bss")));

// ---- frame assembly + transmit (OTG interrupt) ----------------------------------------------------
static void SendFrame(USBD_HandleTypeDef *pdev)
{
    const uint32_t wr   = s_wr;
    const uint32_t fill = (wr - s_rd) / AUDIO_CHANNELS;   // whole frames available

    uint32_t samples = AUDIO_NOM_SAMPLES;
    if(fill > (6U * AUDIO_NOM_SAMPLES))       samples = AUDIO_NOM_SAMPLES + 1U;
    else if(fill < (2U * AUDIO_NOM_SAMPLES))  samples = AUDIO_NOM_SAMPLES - 1U;
    if(samples > AUDIO_MAX_SAMPLES) samples = AUDIO_MAX_SAMPLES;

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
    s_alt = 0; s_streaming = 0; s_wr = s_rd = 0;
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
            // No feature unit -> no controls. Acknowledge an empty data stage, stall anything with data.
            if(req->wLength) { USBD_CtlError(pdev, req); return USBD_FAIL; }
            break;

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
        USBD_LL_FlushEP(pdev, AUDIO_IN_EP);
        SendFrame(pdev);
    }
}

// ---- status ---------------------------------------------------------------------------------------
uint8_t  tt_audio_streaming(void)  { return s_streaming; }
uint32_t tt_audio_frames(void)     { return s_frames; }
uint32_t tt_audio_underruns(void)  { return s_underruns; }
uint32_t tt_audio_overruns(void)   { return s_overruns; }
uint32_t tt_audio_ring_fill(void)  { return (s_wr - s_rd) / AUDIO_CHANNELS; }
