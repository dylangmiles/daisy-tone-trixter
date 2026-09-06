// audio/oled_shim.h — the Pico's oled.h drawing API, implemented on libDaisy's display.
//
// menu.cpp uses exactly THREE of the Pico driver's functions: oled_clear, oled_text and
// oled_text_inv. Providing those on top of libDaisy's OledDisplay means menu.cpp compiles here
// UNCHANGED — the same reasoning as sd_daisy_shim.h. A menu-layout fix on either platform then
// applies to both, instead of the two versions drifting apart.
//
// ⚠ The Pico's async/DMA flush machinery is deliberately NOT reproduced. It existed because the
// RP2350 convolved in a Core 0 foreground loop that a blocking 180 ms I2C flush would stall
// ([[project_oled_async_dma_flush]]). Here audio runs in a DMA-driven callback that the main loop
// cannot stall, so a plain blocking flush is harmless.

#ifndef TT_OLED_SHIM_H
#define TT_OLED_SHIM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
// Bring the panel up. Takes a CONFIGURED I2CHandle -- the bus is already initialised and scanned by
// the time the display is wanted, so the driver borrows it rather than re-initialising it.
namespace daisy { class I2CHandle; }
void tt_oled_init(daisy::I2CHandle i2c, uint8_t addr);

extern "C" {
#endif

void oled_clear(void);                            // clear the framebuffer
void oled_text(int x, int y, const char *s);      // 6x8 text at pixel (x, y)
void oled_text_inv(int x, int y, const char *s);  // inverted — the selected menu row
void oled_flush(void);                            // push to the panel

#ifdef __cplusplus
}
#endif

#endif // TT_OLED_SHIM_H
