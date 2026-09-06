// audio/sd_daisy_shim.cpp — the libDaisy side of the Pico GPIO shim. See sd_daisy_shim.h.

#include "daisy_seed.h"
#include "audio/sd_daisy_shim.h"

using namespace daisy;

// ⚠ libDaisy needs a persistent GPIO object per pin, where the Pico SDK addressed pins by number.
// A small table keyed on the Daisy "D" number bridges the two. Only the SD pins are ever used, but
// the table covers the full D0-D30 range so an out-of-range index cannot corrupt anything.
static constexpr uint32_t kMaxPin = 31;
static GPIO               g_pins[kMaxPin];
static bool               g_inited[kMaxPin] = {false};

static GPIO* Pin_(uint32_t p)
{
    return (p < kMaxPin) ? &g_pins[p] : nullptr;
}

extern "C" {

void tt_shim_gpio_init(uint32_t pin)
{
    if(pin < kMaxPin)
        g_inited[pin] = false;   // direction is set by the following set_dir call
}

void tt_shim_gpio_set_dir(uint32_t pin, int out)
{
    GPIO* g = Pin_(pin);
    if(!g)
        return;
    g->Init(DaisySeed::GetPin((uint8_t)pin),
            out ? GPIO::Mode::OUTPUT : GPIO::Mode::INPUT);
    g_inited[pin] = true;
}

void tt_shim_gpio_put(uint32_t pin, int value)
{
    GPIO* g = Pin_(pin);
    if(g && g_inited[pin])
        g->Write(value != 0);
}

int tt_shim_gpio_get(uint32_t pin)
{
    GPIO* g = Pin_(pin);
    return (g && g_inited[pin]) ? (g->Read() ? 1 : 0) : 0;
}

void tt_shim_gpio_pull_up(uint32_t pin)
{
    GPIO* g = Pin_(pin);
    if(!g)
        return;
    g->Init(DaisySeed::GetPin((uint8_t)pin), GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    g_inited[pin] = true;
}

void tt_shim_gpio_pull_down(uint32_t pin)
{
    GPIO* g = Pin_(pin);
    if(!g)
        return;
    g->Init(DaisySeed::GetPin((uint8_t)pin), GPIO::Mode::INPUT, GPIO::Pull::PULLDOWN);
    g_inited[pin] = true;
}

void tt_shim_busy_wait_us(uint32_t us)
{
    System::DelayUs(us);
}

uint32_t tt_shim_now_ms(void)
{
    return System::GetNow();
}

// ⚠ WRAP-CLEAN MICROSECONDS. System::GetUs() is GetTick() DIVIDED DOWN, so its value rolls over at
// ~21.47e6 rather than at 2^32 -- ordinary unsigned delta arithmetic across that rollover is simply
// wrong. Measured 2026-09-06: `bk stat` reported "max service=4273495447 us", a 4273-second read.
//
// Worse than a silly number, it broke the thing the clock is FOR: backing_service() bounds its work
// with `(time_us_32() - t0) < SERVICE_BUDGET_US`, and across a rollover that comparison goes true
// immediately, so the service returns having done nothing. Once every 21.5 s the backing track was
// being starved for no reason.
//
// Accumulating the RAW tick deltas into a 64-bit counter fixes both: tick deltas are wrap-safe
// (plain 32-bit counter, unsigned subtraction handles it), and the microsecond value derived from
// the accumulator then rolls over cleanly at 2^32, where delta arithmetic is correct again.
//
// ⚠ FOREGROUND ONLY -- the static state is not reentrant. backing.cpp and sd_spi.c are the only
// callers and both run in the foreground loop. Do not call this from an interrupt.
uint32_t tt_shim_now_us(void)
{
    static uint32_t last = 0;
    static uint64_t acc  = 0;
    static uint32_t div  = 0;

    if(div == 0)
    {
        div = System::GetTickFreq() / 1000000u;
        if(div == 0)
            div = 1;
        last = System::GetTick();
    }
    const uint32_t now = System::GetTick();
    acc += (uint32_t)(now - last);      // wrap-safe: raw counter, unsigned subtraction
    last = now;
    return (uint32_t)(acc / div);
}

void tt_shim_sleep_ms(uint32_t ms)
{
    System::Delay(ms);
}

} // extern "C"
