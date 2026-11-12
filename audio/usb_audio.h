// audio/usb_audio.h — USB AUDIO INTERFACE MODE: the Trixie as a class-compliant USB soundcard INPUT.
//
// PHASE 1 (record-only, one-way: pedal -> Mac). A 4-channel UAC 1.0 recording device:
//
//     USB ch 1 / 2  =  MAIN   (the full mix out to the jack: processed guitar + looper + backing),
//                      mono duplicated to the stereo pair
//     USB ch 3      =  CH A DRY  (raw piezo, pre-DSP)
//     USB ch 4      =  CH A WET  (processed guitar only: IR + EQ + comp + out, no looper/backing)
//
// macOS is class-compliant for UAC 1.0, so it appears as a 4-in recording device with NO driver.
//
// ⚠ INCREMENT 2 — now a COMPOSITE Audio+CDC device, always-on when plugged (no gesture). The CDC half
// is libDaisy's own (console + command set unchanged); the audio half is a custom IN class. Both ride
// one device on hUsbDeviceFS; usb_audio_start() replaces StartLog(). See usbd_composite.c (class +
// descriptor + FIFO re-carve) and usbd_audio_in.c (the streaming logic the composite calls).
//
// ⚠ POWER: audio mode runs the full pedal, so it needs the 9 V JACK. On USB power the rail sits
// ~4.9 V and the op-amp daughter's Vref is out of spec -- every sample would be wrong. USB is the
// DATA path only; the 9 V jack powers the analogue front end. Watch for a Mac<->amp ground loop.
//
// ⚠ 16-BIT / 48 kHz in increment 1. A 4-ch 16-bit frame is 48*4*2 = 384 B, which fits EP1-IN's
// existing 512 B TxFIFO with slack for async 49-sample frames. 24-bit (576 B) overflows that FIFO
// and waits until the FIFO map is reworked.
#ifndef TT_USB_AUDIO_H
#define TT_USB_AUDIO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Start the UAC IN device on OTG-FS. Call INSTEAD of StartLog() (they share the port), and -- as with
// drive mode -- before the rest of the system comes up. Returns false if USBD init/register failed.
bool     usb_audio_start(void);

bool     usb_audio_configured(void);   // host enumerated + set a configuration
bool     usb_audio_streaming(void);    // host selected alt-1 on the streaming interface (recording)

// Called ONCE PER BLOCK from the audio callback with the three mono signals of the block.
// master/dry/wet are float in [-1, 1]; n = block size (samples). No-op until the host is streaming.
// Lock-free SPSC: this is the only writer; the OTG IN interrupt is the only reader. Never blocks.
void     usb_audio_push(const float* master, const float* dry, const float* wet, uint32_t n);

uint32_t usb_audio_frames(void);       // iso IN frames transmitted (panel / diag)
uint32_t usb_audio_underruns(void);    // frames sent short because the ring was empty
uint32_t usb_audio_overruns(void);     // blocks dropped at push because the ring was full

// Bring-up diagnostics, same spirit as usb_msc_diag: dev_state, streaming, frames, under/overruns, fill.
void     usb_audio_diag(char* out, int cap);

// Machine-parseable USB-health telemetry for the studio panel (cumulative counters + per-interval
// packet-size histogram + fill range; the histogram/range reset on each call). Line prefix "usbstat ".
void     usb_audio_stat(char* out, int cap);

// ---- REAMP OUT spike (host -> pedal iso-OUT; 2026-11-12) -------------------------------------------
// App side of the OUT stream (defined in usbd_audio_in.c). The audio callback loops received audio to
// the jack to prove the OUT endpoint drains; later this feeds the chain input (bit-exact reamp).
uint8_t  tt_reamp_streaming(void);               // host streaming INTO the pedal (IF4 iso-OUT alt-1)
uint32_t tt_reamp_read(float* out, uint32_t n);  // drain n mono samples to out[] (pads silence); returns got
uint32_t tt_reamp_packets(void);                 // OUT packets received (bench telemetry)
uint32_t tt_reamp_ring_fill(void);               // OUT ring fill (samples)

#ifdef __cplusplus
}
#endif

#endif // TT_USB_AUDIO_H
