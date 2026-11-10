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
uint32_t tt_audio_ring_fill(void);

#ifdef __cplusplus
}
#endif

#endif // TT_AUDIO_STREAM_H
