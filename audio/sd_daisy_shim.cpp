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

void tt_shim_sleep_ms(uint32_t ms)
{
    System::Delay(ms);
}

} // extern "C"
