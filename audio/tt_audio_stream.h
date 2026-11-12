// audio/tt_audio_stream.h — geometry + ops for the USB audio-IN streaming function.
//
// Increment 2: the audio function is now one half of a COMPOSITE Audio+CDC device (usbd_composite.c).
// The streaming logic (ring, push, iso-IN drain, alt handling) lives in usbd_audio_in.c; the composite
// class calls the tt_audio_* ops below and embeds the descriptor fragment built from these defines.
#ifndef TT_AUDIO_STREAM_H
#define TT_AUDIO_STREAM_H

#include <stdint.h>
#include "usbd_def.h"   // USBD_HandleTypeDef, USBD_SetupReqTypedef

#ifdef __cplusplus
extern "C" {
#endif

// ---- geometry -------------------------------------------------------------------------------------
// ⚠ AUDIO_BITS: bring composite up at 16 (one new variable at a time), flip to 24 once it enumerates.
// The ring stores 24-bit precision regardless; only the wire packing + descriptor change with this.
#define AUDIO_BITS          24U
#define AUDIO_SUBFRAME      (AUDIO_BITS / 8U)         // bytes/sample on the wire: 2 or 3
#define AUDIO_CHANNELS      4U                        // ch1/2 Main, ch3 Dry, ch4 Wet
#define AUDIO_FREQ          48000U
#define AUDIO_NOM_SAMPLES   48U                       // samples/frame nominal (async nudges ±1)
#define AUDIO_MAX_SAMPLES   50U
#define AUDIO_MAX_PACKET    (AUDIO_MAX_SAMPLES * AUDIO_CHANNELS * AUDIO_SUBFRAME)

// ⚠ Endpoint + interface numbers in the COMPOSITE layout. CDC owns 0x81/0x01/0x82 and interfaces 0/1
// (so libDaisy's CDC/logger/UsbRx keep working unchanged); audio takes EP 0x83 and interfaces 2/3.
#define AUDIO_IN_EP         0x83U
#define AUDIO_AC_INTERFACE  2U
#define AUDIO_AS_INTERFACE  3U

// ---- UAC2 entity IDs (clock-source port, 2026-11-10) ----------------------------------------------
// macOS does smooth async sample-rate conversion only when the device declares a UAC2 Clock Source
// entity it can query for the rate; UAC1 (no clock entity) made it drop a sample every ~2 s.
#define AUDIO_IT_ID         0x01U   // Input Terminal (microphone)
#define AUDIO_OT_ID         0x02U   // Output Terminal (USB streaming)
#define AUDIO_CLOCK_ID      0x03U   // Clock Source entity

// ---- REAMP OUT spike (host -> pedal iso-OUT, 2026-11-12) -------------------------------------------
// A second AudioStreaming interface (IF4) + iso-OUT endpoint carries a mono stream from the host INTO
// the pedal. Spike goal: prove the OUT endpoint enumerates + drains; the app loops it straight to the
// jack (tt_reamp_read). Later this becomes the bit-exact reamp inject at the chain input.
// ⚠ ADAPTIVE sync (no feedback endpoint) for the spike — minimal descriptor, no extra FIFO. The mono
// 24-bit OUT packet (<=150 B) fits the existing RxFIFO (0x50=320 B) so no FIFO re-carve is needed.
#define REAMP_OUT_EP          0x03U    // iso OUT (host -> device)
#define AUDIO_AS_OUT_INTERFACE 4U      // 2nd AudioStreaming interface (the OUT stream)
#define REAMP_IT_ID           0x04U    // Input Terminal: USB streaming (OUT audio enters here)
#define REAMP_OT_ID           0x05U    // Output Terminal: to the pedal (jack / chain input)
#define REAMP_CHANNELS        1U       // mono DRY reamp source
#define REAMP_MAX_PACKET      (AUDIO_MAX_SAMPLES * REAMP_CHANNELS * AUDIO_SUBFRAME)  // 50*1*3 = 150

// ---- ops the composite class calls ----------------------------------------------------------------
void    tt_audio_init(void);                                    // reset static state (composite Init)
void    tt_audio_deinit(USBD_HandleTypeDef* pdev);              // close EP if open (composite DeInit)
uint8_t tt_audio_setup(USBD_HandleTypeDef* pdev, USBD_SetupReqTypedef* req);  // IF2/IF3 requests
void    tt_audio_datain(USBD_HandleTypeDef* pdev);              // iso-IN complete -> queue next frame
void    tt_audio_isoincomplete(USBD_HandleTypeDef* pdev);       // missed IN token -> re-arm

// ---- producer + status (also declared in usb_audio.h for app use) ---------------------------------
uint8_t  tt_audio_streaming(void);
uint32_t tt_audio_frames(void);
uint32_t tt_audio_underruns(void);
uint32_t tt_audio_overruns(void);
uint32_t tt_audio_isoincomp(void);
uint32_t tt_audio_recenter(void);
uint32_t tt_audio_sof_count(void);
void     tt_audio_sof(void);
void     tt_audio_hist_take(uint32_t out[4]);
void     tt_audio_fill_range_take(uint32_t *mn, uint32_t *mx);
uint32_t tt_audio_ring_fill(void);

// ---- REAMP OUT ops (IF4 / EP 0x03) ----------------------------------------------------------------
void     tt_reamp_init(void);                                   // reset OUT state (composite Init)
void     tt_reamp_deinit(USBD_HandleTypeDef* pdev);             // close OUT EP (composite DeInit)
uint8_t  tt_reamp_setup(USBD_HandleTypeDef* pdev, USBD_SetupReqTypedef* req);   // IF4 requests
void     tt_reamp_dataout(USBD_HandleTypeDef* pdev, uint8_t epnum);             // iso-OUT packet in
void     tt_reamp_isoincomplete(USBD_HandleTypeDef* pdev);      // missed OUT token -> re-arm
uint32_t tt_reamp_read(float* out, uint32_t n);                 // app: drain n mono samples (loopback)
uint8_t  tt_reamp_streaming(void);
uint32_t tt_reamp_packets(void);                                // telemetry: OUT packets received
uint32_t tt_reamp_ring_fill(void);                              // OUT ring fill (samples)

#ifdef __cplusplus
}
#endif

#endif // TT_AUDIO_STREAM_H
