// audio/usb_msc.h — USB DRIVE MODE: the SD card as a USB mass-storage device.
//
// Entered at boot only (encoder held while powering up -- main.cpp), before FatFs mounts anything,
// and left only by a power cycle. The host has raw block access and caches what it reads; the
// pedal must never mount the card while a host holds it, and never hand the card back to a host
// after mounting it. Both rules fall out of "boot in, power-cycle out" and nothing else enforces
// them.
#ifndef TT_USB_MSC_H
#define TT_USB_MSC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool     usb_msc_start(void);        // sd_init() must already have succeeded
bool     usb_msc_configured(void);   // host has enumerated and configured us
uint32_t usb_msc_reads(void);        // blocks served, for the panel
uint32_t usb_msc_writes(void);
uint32_t usb_msc_errors(void);

#ifdef __cplusplus
}
#endif

#endif // TT_USB_MSC_H
