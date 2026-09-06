// audio/oled_shim.cpp — SH1106 128x64 over I2C, with BATCHED page writes.
//
// ⚠ WHY WE DO NOT USE libDaisy's OledDisplay HERE.
//
// libDaisy's SSD130xI2CTransport::SendData sends ONE BLOCKING I2C TRANSACTION PER BYTE
// (src/dev/oled_ssd130x.h): 1024 transactions for a 128x64 frame, each carrying an address, a
// control byte and ACKs. That is roughly 77 ms per full flush at 400 kHz.
//
// Measured consequence on 2026-09-06: with the menu redrawing at 100 ms the foreground loop was
// ~77 % blocked, which starved g_enc.Debounce() (encoder missed clicks), starved backing_service()
// (backing track broke up), and made the menu itself feel slow. The home screen redrew at 500 ms
// and was fine -- which is exactly the clue that pointed here.
//
// This driver writes a whole PAGE per transaction: 8 pages = 16 transactions instead of 1024, a
// ~60x reduction. Same panel, same bus, same framebuffer semantics.
//
// Font data is borrowed from libDaisy's oled_fonts (Font_6x8) rather than duplicated.

#include "daisy_seed.h"
#include "util/oled_fonts.h"
#include "audio/oled_shim.h"

#include <cstring>

using namespace daisy;

static constexpr int kW     = 128;
static constexpr int kH     = 64;
static constexpr int kPages = kH / 8;

static I2CHandle g_i2c;
static uint8_t   g_addr = 0x3C;
static bool      g_up   = false;

// Framebuffer, one bit per pixel, column-major within each 8-row page — the SH1106's native layout,
// so a flush is a straight memcpy rather than a transpose.
static uint8_t g_fb[kW * kPages];

// ⚠ Shadow of what the PANEL actually holds, so a flush sends only the pages whose content really
// changed. menu_render() clears and redraws the whole framebuffer every time, so "was it written"
// is useless as a dirty test -- everything is. Comparing against what was last SENT is the only
// thing that distinguishes a moved menu row from a redraw of identical pixels.
//
// Menu navigation typically changes one or two rows: 2 pages instead of 8, so ~6 ms instead of 24.
static uint8_t g_shadow[kW * kPages];
static bool    g_shadow_valid = false;

// ⚠ SH1106 is NOT an SSD1306: its RAM is 132 columns wide and the visible 128 are centred, so every
// page write starts at column 2. Getting this wrong shifts the image by two pixels and wraps the
// right-hand edge around to the left.
static constexpr uint8_t kColOffset = 2;

static void Cmd(uint8_t c)
{
    uint8_t b[2] = {0x00, c};
    g_i2c.TransmitBlocking(g_addr, b, 2, 100);
}

void tt_oled_init(I2CHandle i2c, uint8_t addr)
{
    g_i2c  = i2c;
    g_addr = addr;

    static const uint8_t init[] = {
        0xAE,              // display off
        0xD5, 0x80,        // clock divide
        0xA8, 0x3F,        // multiplex = 63
        0xD3, 0x00,        // display offset
        0x40,              // start line 0
        0xAD, 0x8B,        // SH1106 charge pump on
        0xA1,              // segment remap
        0xC8,              // COM scan direction
        0xDA, 0x12,        // COM pins
        0x81, 0x80,        // contrast
        0xD9, 0x22,        // pre-charge
        0xDB, 0x35,        // VCOM deselect
        0xA4,              // resume from RAM
        0xA6,              // normal (not inverted)
        0xAF,              // display on
    };
    for(size_t i = 0; i < sizeof(init); i++)
        Cmd(init[i]);

    memset(g_fb, 0, sizeof(g_fb));
    g_up = true;
    oled_flush();
}

extern "C" {

void oled_clear(void)
{
    memset(g_fb, 0, sizeof(g_fb));
}

void oled_flush(void)
{
    if(!g_up)
        return;

    // ⚠ One transaction per PAGE, and only for pages that actually changed.
    static uint8_t line[1 + kW];
    line[0] = 0x40;                       // data control byte, then 128 bytes of pixels
    for(int p = 0; p < kPages; p++)
    {
        const uint8_t* src = &g_fb[p * kW];
        if(g_shadow_valid && memcmp(src, &g_shadow[p * kW], kW) == 0)
            continue;                     // identical to what the panel already shows

        Cmd(0xB0 | p);                                    // page address
        Cmd(0x00 | (kColOffset & 0x0F));                  // column low nibble
        Cmd(0x10 | (kColOffset >> 4));                    // column high nibble
        memcpy(&line[1], src, kW);
        g_i2c.TransmitBlocking(g_addr, line, sizeof(line), 100);
        memcpy(&g_shadow[p * kW], src, kW);
    }
    g_shadow_valid = true;
}

static void PixelSet(int x, int y, bool on)
{
    if(x < 0 || x >= kW || y < 0 || y >= kH)
        return;
    uint8_t& b = g_fb[(y / 8) * kW + x];
    const uint8_t m = (uint8_t)(1u << (y & 7));
    if(on)
        b |= m;
    else
        b = (uint8_t)(b & ~m);
}

static void DrawChar(int x, int y, char c, bool on)
{
    if(c < 32 || c > 126)
        c = ' ';
    const FontDef& f = Font_6x8;
    // libDaisy's font data is row-major: one uint16 per row, bit 15 = column 0.
    for(int row = 0; row < f.FontHeight; row++)
    {
        const uint16_t bits = f.data[(c - 32) * f.FontHeight + row];
        for(int col = 0; col < f.FontWidth; col++)
        {
            const bool lit = ((bits << col) & 0x8000) != 0;
            PixelSet(x + col, y + row, lit ? on : !on);
        }
    }
}

void oled_pixel(int x, int y, bool on)
{
    PixelSet(x, y, on);
}

// ⚠ Doubled 6x8 -> 12x16, for the tuner's note. A tuner is read at arm's length with a guitar in
// the way, so the note has to be legible without leaning in -- which the 6x8 font is not.
static void DrawChar2x(int x, int y, char c)
{
    if(c < 32 || c > 126)
        c = ' ';
    const FontDef& f = Font_6x8;
    for(int row = 0; row < f.FontHeight; row++)
    {
        const uint16_t bits = f.data[(c - 32) * f.FontHeight + row];
        for(int col = 0; col < f.FontWidth; col++)
        {
            const bool lit = ((bits << col) & 0x8000) != 0;
            // one source pixel becomes a 2x2 block, background included -- same convention as
            // DrawChar, so a redraw overwrites what was there instead of merging with it.
            PixelSet(x + col * 2,     y + row * 2,     lit);
            PixelSet(x + col * 2 + 1, y + row * 2,     lit);
            PixelSet(x + col * 2,     y + row * 2 + 1, lit);
            PixelSet(x + col * 2 + 1, y + row * 2 + 1, lit);
        }
    }
}

void oled_text2x(int x, int y, const char* s)
{
    if(!s)
        return;
    for(int i = 0; s[i]; i++)
        DrawChar2x(x + i * 12, y, s[i]);
}

void oled_rect(int x, int y, int w, int h, bool on)
{
    for(int j = 0; j < h; j++)
        for(int i = 0; i < w; i++)
            PixelSet(x + i, y + j, on);
}

// Outlined bar with a filled proportion. ⚠ An outline matters: a bare filled block gives no sense
// of scale, so a quiet signal and a broken meter look identical.
void oled_bar(int x, int y, int w, int h, float frac)
{
    if(frac < 0.f) frac = 0.f;
    if(frac > 1.f) frac = 1.f;
    for(int i = 0; i < w; i++)
    {
        PixelSet(x + i, y, true);
        PixelSet(x + i, y + h - 1, true);
    }
    for(int j = 0; j < h; j++)
    {
        PixelSet(x, y + j, true);
        PixelSet(x + w - 1, y + j, true);
    }
    int fill = (int)((w - 2) * frac + 0.5f);
    if(fill > 0)
        oled_rect(x + 1, y + 1, fill, h - 2, true);
}

void oled_text(int x, int y, const char* s)
{
    if(!s)
        return;
    for(int i = 0; s[i]; i++)
        DrawChar(x + i * 6, y, s[i], true);
}

void oled_text_inv(int x, int y, const char* s)
{
    if(!s)
        return;
    // Inverted: the selected menu row. Fill the cell then draw the glyph "off", so the row reads at
    // a glance -- without it every row looks identical and the menu is unusable.
    for(int i = 0; s[i]; i++)
        DrawChar(x + i * 6, y, s[i], false);
}

} // extern "C"
