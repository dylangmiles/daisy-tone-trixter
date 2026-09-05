// audio/sd_daisy_shim.h — Pico GPIO calls, implemented on libDaisy.
//
// WHY A SHIM RATHER THAN A REWRITE: sd_spi.c is 229 lines of working SD card protocol — command
// framing, the CMD0/CMD8/ACMD41 init dance, CSD parsing, response timeouts. That logic is
// platform-independent and hard-won; only the half-dozen GPIO calls underneath it are Pico-specific.
// Shimming those keeps the driver source IDENTICAL between the two builds, so a protocol fix made
// on either platform applies to both. Rewriting it would fork the file and guarantee they drift.
//
// ⚠ The PIN NUMBERS do change (see sd_spi.h) — they are Daisy "D" numbers here, not RP2350 GPIOs.
// The shim maps them through DaisySeed::GetPin().

#ifndef TT_SD_DAISY_SHIM_H
#define TT_SD_DAISY_SHIM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Direction constants, spelled as the Pico SDK spells them.
#define GPIO_IN  0
#define GPIO_OUT 1

void tt_shim_gpio_init(uint32_t pin);
void tt_shim_gpio_set_dir(uint32_t pin, int out);
void tt_shim_gpio_put(uint32_t pin, int value);
int  tt_shim_gpio_get(uint32_t pin);
void tt_shim_gpio_pull_up(uint32_t pin);
void tt_shim_gpio_pull_down(uint32_t pin);
void     tt_shim_busy_wait_us(uint32_t us);
uint32_t tt_shim_now_ms(void);
void tt_shim_sleep_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

// Map the Pico SDK names onto the shim, so sd_spi.c needs no edits beyond its include.
#define gpio_init(p)        tt_shim_gpio_init(p)
#define gpio_set_dir(p, d)  tt_shim_gpio_set_dir((p), (d))
#define gpio_put(p, v)      tt_shim_gpio_put((p), (v))
#define gpio_get(p)         tt_shim_gpio_get(p)
#define gpio_pull_up(p)     tt_shim_gpio_pull_up(p)
#define gpio_pull_down(p)   tt_shim_gpio_pull_down(p)
#define busy_wait_us(u)     tt_shim_busy_wait_us(u)

// ⚠ Pico timeout API, mapped onto a plain millisecond counter. The Pico's absolute_time_t is an
// opaque 64-bit type with helper functions; here a deadline is just "now + ms". Kept as macros with
// the same names so sd_spi.c's timeout logic is untouched -- those timeouts are load-bearing (a card
// that never answers must fail rather than hang the caller).
typedef uint32_t absolute_time_t;
#define make_timeout_time_ms(ms)  (tt_shim_now_ms() + (uint32_t)(ms))
#define time_reached(t)           ((int32_t)(tt_shim_now_ms() - (t)) >= 0)
#define sleep_ms(m)         tt_shim_sleep_ms(m)

#endif // TT_SD_DAISY_SHIM_H
