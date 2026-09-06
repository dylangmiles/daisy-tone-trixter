// audio/oled_shim.cpp — see oled_shim.h.
//
// The display object itself lives in main.cpp (it is initialised there, after the I2C scan proves
// the panel is present). These functions reach it through a registered pointer rather than owning
// it, so there is exactly one display instance and no question about which one is current.

#include "daisy_seed.h"
#include "dev/oled_sh1106.h"
#include "audio/oled_shim.h"

using namespace daisy;

using TtOledT = OledDisplay<SH1106I2c128x64Driver>;

static TtOledT* g_disp = nullptr;

// Called from main.cpp once the display is up.
void tt_oled_shim_bind(TtOledT* d)
{
    g_disp = d;
}

extern "C" {

void oled_clear(void)
{
    if(g_disp)
        g_disp->Fill(false);
}

void oled_text(int x, int y, const char* s)
{
    if(!g_disp || !s)
        return;
    g_disp->SetCursor((uint16_t)x, (uint16_t)y);
    g_disp->WriteString(s, Font_6x8, true);
}

void oled_text_inv(int x, int y, const char* s)
{
    if(!g_disp || !s)
        return;
    // ⚠ Inversion is a filled bar plus text drawn "off". libDaisy has no inverted-text call, and
    // this is what the selected menu row depends on to be readable at a glance -- without it every
    // row looks identical and the menu is unusable.
    int len = 0;
    while(s[len])
        len++;
    const int w = len * 6;
    g_disp->DrawRect((uint_fast8_t)x,
                     (uint_fast8_t)y,
                     (uint_fast8_t)(x + w - 1),
                     (uint_fast8_t)(y + 7),
                     true,
                     true);
    g_disp->SetCursor((uint16_t)x, (uint16_t)y);
    g_disp->WriteString(s, Font_6x8, false);
}

void oled_flush(void)
{
    if(g_disp)
        g_disp->Update();
}

} // extern "C"
