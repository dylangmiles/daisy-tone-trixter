// Tone Trixter — Daisy Seed3 bring-up diagnostics.
//
// Purpose: prove the board is alive and correctly wired BEFORE any audio or DSP code exists.
// Every check here answers one question and reports pass/fail on its own, so a failure points at
// a specific joint rather than "it doesn't work".
//
// This follows the project's hard-won bench rule: separate the variables. On the Pico build,
// three separate faults (a compressor left on, an uncompensated probe, a debug probe radiating
// into a high-Z input) each produced a believable-but-wrong answer because more than one thing
// was in play at once. Diagnostics exist to make that impossible.
//
// Flash:  make program-dfu   (hold BOOT, tap RESET, then run)
// Watch:  screen /dev/tty.usbmodem* 115200   — or any serial terminal at 115200

#include "daisy_seed.h"
#include "dev/oled_sh1106.h"
#include "board.h"

// ⚠ NO extern "C" wrapper: dsp_chain.cpp compiles as C++ here, so both sides must agree on
// linkage. The header is C-style but the translation unit is C++.
#include "audio/dsp_chain.h"
#include "audio/tt_store.h"
#include "audio/wav_load.h"
#include "audio/tuner.h"
#include "audio/menu.h"
#include "audio/app_hooks.h"
#include "audio/oled_shim.h"
extern "C" {
#include "ff.h"
#include "ff_gen_drv.h"
extern const Diskio_drvTypeDef tt_sd_driver;
}
#include "TwoStageFFTConvolver.h"

#include <cstdio>
#include <cmath>
#include <cstring>

using namespace daisy;

static DaisySeed hw;

// ---------------------------------------------------------------------------------------------
// OLED readout.
//
// ⚠ THIS EXISTS SO THE BOARD CAN BE DIAGNOSED WITHOUT USB. Audio work must run from the 9 V jack:
// on USB the Daisy back-feeds VIN to ~4.9 V, which puts the op-amp daughter's Vref (1.57 V) ABOVE
// its common-mode ceiling (1.41 V) -- the exact out-of-spec condition that measured 8.9 % THD on
// the 5 V build. Every audio result taken on USB power would be wrong, and wrong in a way that
// looks like a front-end fault. The serial console only exists on USB, so it cannot be the way the
// board reports during audio testing.
using TtOled = OledDisplay<SH1106I2c128x64Driver>;
void tt_oled_shim_bind(TtOled* d);   // audio/oled_shim.cpp
static TtOled oled;
static bool   oled_ok = false;

// Boot-check results, kept so a short press of the encoder switch can re-print them. ⚠ The boot
// report itself is routinely LOST: StartLog(false) starts printing before the USB CDC device has
// enumerated, so the host attaches mid-banner and everything before that point never arrives
// (seen as "===========$$"). On-demand re-print is the fix; blocking on the host is not.
static bool g_board_ok = false, g_i2c_ok = false, g_sd_ok = false;

// ---------------------------------------------------------------------------------------------
// Audio passthrough + input meter.
//
// AUDIO IN 1 -> AUDIO OUT 1 and 2, unmodified. No DSP, no gain: this measures the ANALOGUE PATH
// and nothing else, so anything it shows is the front end, the jacks or the codec.
//
// ⚠ Test the codec BEFORE the daughter is fitted, by linking daughter pin 4 (gate-in) to pin 3
// (out) in the socket. That bypasses the buffer and proves the Daisy's own audio in/out. Fit the
// daughter after, and any change is unambiguously the front end.
//
// ⚠ 0 dBFS here is the CODEC's full scale (±1.8 V at the pin), NOT the buffer's clean limit. The
// op-amp daughter rev D clears ~2.5 V at a 8.86 V rail, so the converter clips first -- which is
// the intended design, clipping in the fixable place rather than the buffer.
// ---- DSP chain, ported from the RP2350 build ----------------------------------------------
//
// dsp_chain.cpp, biquad.h and the HiFi-LoFi FFTConvolver compiled for the H750 with NO changes --
// the Pico implementation was written against stdio/string/stdlib/math only.
//
// ⚠ ORDER MATTERS, and it matches the Pico: IR convolution FIRST, then the chain
// (EQ -> Dynamics -> Output level). See dsp_chain.h and pico/main.cpp:1622.
//
// ⚠ DEFAULT BOOT STATE IS COMPLETE PASSTHROUGH -- global bypass on, no IR. The pedal always starts
// transparent, so anything measured at boot reflects the FRONT END ALONE with no DSP on top of it.
// That matters while the analogue path is still being characterised.
//
// ⚠ The IR is NOT embedded. It lives on the SD card and is loaded when a preset asks for one, so
// nothing here costs flash until it is actually wanted.
static fftconvolver::TwoStageFFTConvolver g_convolver;

// ---- SD card: presets and IRs ---------------------------------------------------------------
//
// ⚠ Everything here is OPTIONAL. No card, no presets, no IR -- the pedal still boots and passes
// audio. A missing SD card must never be the difference between a working pedal and a dead one.
static FATFS g_fs;
static char  g_sd_path[4]     = {0};
static bool  g_sd_mounted     = false;
static int   g_preset_count   = 0;

// ⚠ 4096 taps of scratch, matching the Pico's SD_IR_MAX_TAPS. The convolver COPIES the IR in, so
// this buffer is only needed during load -- but it stays static because 16 KB on the stack would
// overflow it.
static constexpr int kIrMaxTaps = 4096;
static float         g_ir_buf[kIrMaxTaps];
static int           g_ir_len  = 0;
static char          g_ir_name[24] = "none";

// ⚠ The OUTCOME of the SD phase, captured as it happens. The boot report is routinely lost to USB
// CDC enumeration, so a failure reason that only prints at boot is a reason nobody reads. This has
// now bitten three times (block size, the SD check, and the SD result itself) -- so the rule is:
// every check records its verdict here, and PrintFullReport() shows it.
static char g_sd_status[64] = "not attempted";
static int  g_preset_idx = 0;

// ⚠ libDaisy's Encoder owns A/B/SW and does the quadrature decoding. The raw-GPIO monitoring that
// proved those joints during bring-up has served its purpose -- keeping both would fight over the
// pins. Footswitches stay raw GPIO.
static Encoder g_enc;

// Tuner. ⚠ Fed from the RAW INPUT, before any DSP: you tune the string, not the compressed and
// EQ'd version of it. Gated behind g_tuner_on so its autocorrelation costs nothing when unused --
// on the Pico the equivalent was a `tuner on/off` command for the same reason.
static volatile bool g_tuner_on = false;
static TunerResult   g_tune     = {};

// Convolver partitioning, same as the Pico. Head block matches the audio block size so the
// low-latency path costs exactly one block.
static constexpr size_t kIrHeadBlock = 64;
static constexpr size_t kIrTailBlock = 512;
static volatile bool                      g_ir_active = false;   // false until an IR is loaded

static constexpr size_t kBlock    = 64;   // FFT convolution wants a real block; 64 = 1.33 ms @ 48k
static constexpr size_t kWorkMax = 256;  // libDaisy's ceiling (kAudioMaxBufferSize/4). Sized to the
                                         // MAXIMUM, not to kBlock: if the hardware ever hands us a
                                         // bigger block than we asked for, a kBlock-sized buffer
                                         // would overflow silently and corrupt whatever follows.
static float            g_work[kWorkMax];

// What the callback ACTUALLY receives, surfaced in the repeating summary line. ⚠ The boot report is
// routinely lost to USB CDC enumeration, so a number that only prints at boot is a number nobody
// reads -- which is how the block size stayed unverified while we guessed at it.
static volatile uint32_t g_cb_size = 0;
static volatile uint32_t g_cb_count = 0;

static volatile float    g_peak  = 0.f;   // peak |sample| since the display last read it
static volatile uint32_t g_clips = 0;

static void AudioCb(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    float pk = 0.f;

    g_cb_size = (uint32_t)size;
    g_cb_count++;
    if(size > kWorkMax)
        return;                             // refuse rather than overflow

    // Meter the INPUT before any processing -- this is the front end's level, not the chain's.
    for(size_t i = 0; i < size; i++)
    {
        float a = fabsf(in[0][i]);
        if(a > pk)
            pk = a;
        if(a >= 0.99f)
            g_clips++;
        g_work[i] = in[0][i];
    }
    if(pk > g_peak)
        g_peak = pk;

    if(g_tuner_on && tuner_feed(g_work, (int)size))
        g_tune = tuner_result();

    // IR first, then the chain -- same order as the Pico build.
    if(g_ir_active && dsp_chain_ir_enabled())
        g_convolver.process(g_work, g_work, size);

    dsp_chain_process(g_work, (int)size);   // EQ -> Dynamics -> Output level (all off when bypassed)

    for(size_t i = 0; i < size; i++)
    {
        out[0][i] = g_work[i];              // mono out to both channels
        out[1][i] = g_work[i];
    }
}

// Inputs that read LOW when active. Encoder C and SW2 go to the DGND rail, and the footswitches
// ground their signal line, so every one of these needs an internal pull-up.
struct Input
{
    const char* name;
    uint8_t     pin;
    GPIO        gpio;
    bool        last;
};

// ⚠ FOOTSWITCHES ONLY. A/B/SW moved to libDaisy's Encoder, which does the quadrature decoding --
// the raw-GPIO monitoring proved those three joints during bring-up and has done its job.
static Input inputs[] = {
    {"footsw bypass", tt::kFswBypass, {}, true},
    {"footsw tuner", tt::kFswTuner, {}, true},
};
constexpr size_t kNumInputs = sizeof(inputs) / sizeof(inputs[0]);

static const char* BoardName(DaisySeed::BoardVersion v)
{
    switch(v)
    {
        case DaisySeed::BoardVersion::DAISY_SEED: return "Daisy Seed (rev4)";
        case DaisySeed::BoardVersion::DAISY_SEED_1_1: return "Daisy Seed 1.1";
        case DaisySeed::BoardVersion::DAISY_SEED_2_DFM: return "Daisy Seed 2 DFM";
        case DaisySeed::BoardVersion::DAISY_SEED_1_2: return "Daisy Seed 1.2";
        case DaisySeed::BoardVersion::DAISY_SEED_3: return "Daisy Seed3";
        default: return "unknown";
    }
}

// ---------------------------------------------------------------------------------------------
// Test 1 — board identity.
// Seed3 is detected at RUNTIME (PH6 tied to GND), not by a compile-time define. If this reports
// anything else, libDaisy will configure the wrong codec and every audio result would be junk.
static bool CheckBoard()
{
    auto v = hw.CheckBoardVersion();
    hw.PrintLine("  detected: %s", BoardName(v));
    return v == DaisySeed::BoardVersion::DAISY_SEED_3;
}

// ---------------------------------------------------------------------------------------------
// Test 2 — I2C bus scan.
// Proves the OLED's converted links, its address strap, and the two cut-and-jumper power runs, in
// one shot and WITHOUT any display driver. A blank screen cannot tell a bad link from a driver
// bug; an address answering can.
static bool ScanI2c(I2CHandle& i2c)
{
    int  found      = 0;
    bool saw_target = false;

    for(uint8_t addr = 0x08; addr < 0x78; addr++)
    {
        // Zero-length write is the standard probe: it addresses the device and looks for the ACK
        // without writing a register. libDaisy takes 7-BIT addresses and shifts internally.
        if(i2c.TransmitBlocking(addr, nullptr, 0, 10) == I2CHandle::Result::OK)
        {
            hw.PrintLine("  responder at 0x%02X%s",
                         addr,
                         addr == tt::kOledI2cAddr ? "   <- OLED, as expected" : "");
            found++;
            if(addr == tt::kOledI2cAddr)
                saw_target = true;
        }
    }

    if(found == 0)
        hw.PrintLine("  NOTHING on the bus.");
    if(!saw_target)
    {
        // 0x3D is the same display with DC strapped high instead of low — a wrong-rail jumper,
        // not a dead module. Worth calling out by name so it is not mistaken for a bus fault.
        hw.PrintLine("  0x%02X did not answer.", tt::kOledI2cAddr);
        hw.PrintLine("  If 0x3D answered, the DC strap went to +3V3D instead of ground.");
        hw.PrintLine("  If nothing answered, check the row 19 (GND) and row 20 (VCC) jumpers first");
        hw.PrintLine("  -- they are the only OLED pins needing a cut AND a jumper.");
    }
    return saw_target;
}

// ---------------------------------------------------------------------------------------------
// Test 4 — microSD bus, by bit-banged SPI CMD0.
//
// ⚠ BIT-BANGED DELIBERATELY. The breakout presents a FIXED pin order that matches no hardware SPI
// instance on the Seed3 (§3b), so there is no peripheral to use. These are plain GPIOs.
//
// This sends CMD0 (GO_IDLE_STATE) and reads the R1 response. That exercises CS, SCK, MOSI and MISO
// in one shot, and the FAILURE MODES ARE DISTINGUISHABLE, which is the whole point:
//
//   0x01  card is in idle state  -> the bus works end to end
//   0xFF  all ones, no response  -> nothing driving MISO: no card, or CS/SCK/MOSI not reaching it
//   0x00  all zeros              -> ⚠ MISO STUCK LOW. On the Pico build this exact reading was a
//                                  solder bridge from MISO to ground, misread for hours as "card
//                                  not responding" ([[project_sd_card_pinout]]). Meter D6 to GND
//                                  before blaming the card or the module.
//
// ⚠ Breakout silkscreen is CARD-referenced: its "DO" is the card's data OUT = MISO at the host, and
// "DI" is data IN = MOSI. Wiring them by name is how they end up swapped.
static GPIO    sd_cs, sd_sck, sd_mosi, sd_miso, sd_cd;
static uint8_t g_sd_r1 = 0xFF;   // last CMD0 response, shown in the periodic summary
static bool    g_sd_cd = false;  // card-detect level, ditto

// Slow clock on purpose: cards must be initialised at 100-400 kHz, not at full speed.
static void SdClk(bool v)
{
    sd_sck.Write(v);
    System::DelayUs(2);
}

static uint8_t SdByte(uint8_t out)
{
    uint8_t in = 0;
    for(int i = 7; i >= 0; i--)
    {
        sd_mosi.Write((out >> i) & 1);
        SdClk(true);                            // mode 0: sample on the rising edge
        in = (uint8_t)((in << 1) | (sd_miso.Read() ? 1 : 0));
        SdClk(false);
    }
    return in;
}

static bool TestSd()
{
    sd_cs.Init(tt::P(tt::kSdCs), GPIO::Mode::OUTPUT);
    sd_sck.Init(tt::P(tt::kSdSck), GPIO::Mode::OUTPUT);
    sd_mosi.Init(tt::P(tt::kSdMosi), GPIO::Mode::OUTPUT);
    sd_miso.Init(tt::P(tt::kSdMiso), GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    sd_cd.Init(tt::P(tt::kSdCd), GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    System::Delay(10);                          // settle the pull-ups before the first read

    g_sd_cd = sd_cd.Read();
    hw.PrintLine("  card detect (D%d): %s", tt::kSdCd, g_sd_cd ? "HIGH" : "LOW");

    // Power-up: >=74 clocks with CS HIGH and MOSI high, before the card will talk at all.
    sd_cs.Write(true);
    sd_sck.Write(false);
    for(int i = 0; i < 10; i++)
        SdByte(0xFF);

    // CMD0 (GO_IDLE_STATE), with its fixed CRC. CRC matters here: it is still checked in SPI mode
    // until CMD59 turns checking off.
    sd_cs.Write(false);
    const uint8_t cmd0[] = {0x40, 0x00, 0x00, 0x00, 0x00, 0x95};
    for(size_t i = 0; i < sizeof(cmd0); i++)
        SdByte(cmd0[i]);

    uint8_t r = 0xFF;
    for(int i = 0; i < 16 && r == 0xFF; i++)
        r = SdByte(0xFF);

    sd_cs.Write(true);
    SdByte(0xFF);                               // release: the card needs a clock to let go of MISO

    g_sd_r1 = r;
    hw.PrintLine("  CMD0 response: 0x%02X", r);
    if(r == 0x01)
        return true;
    if(r == 0x00)
        hw.PrintLine("  ⚠ ALL ZEROS = MISO stuck low. Meter D%d to GND before suspecting the card.",
                     tt::kSdMiso);
    else if(r == 0xFF)
        hw.PrintLine("  no response -- card absent, or CS/SCK/MOSI not reaching the module.");
    else
        hw.PrintLine("  unexpected -- card answered but not with idle state.");
    return false;
}

// 128x64 with Font_6x8 gives 8 rows of 21 characters. Everything a person needs while the board is
// running on 9 V with no console must fit here.
static void OledLine(int row, const char* text)
{
    oled.SetCursor(0, (uint16_t)(row * 8));
    oled.WriteString(text, Font_6x8, true);
}

static void OledReport(bool board_ok,
                       bool i2c_ok,
                       bool sd_ok,
                       uint8_t sd_r1,
                       bool sd_cd_level,
                       uint32_t up_s)
{
    if(!oled_ok)
        return;

    char buf[24];
    oled.Fill(false);

    OledLine(0, "TONE TRIXTER SEED3");
    OledLine(1, board_ok ? "board Seed3      OK" : "board WRONG    FAIL");

    snprintf(buf, sizeof(buf), "i2c   0x%02X       %s", tt::kOledI2cAddr, i2c_ok ? "OK" : "--");
    OledLine(2, buf);

    snprintf(buf, sizeof(buf), "sd  CMD0=%02X %s", sd_r1,
             sd_ok ? "     OK" : (sd_r1 == 0x00 ? " MISOLOW" : "  nocard"));
    OledLine(3, buf);

    snprintf(buf, sizeof(buf), "cd  %s", sd_cd_level ? "HIGH" : "LOW");
    OledLine(4, buf);

    // Live input state. '#' = pressed (LOW), '.' = idle. Reading the pins directly rather than the
    // cached edge state, so the display always shows NOW.
    // Tuner display when armed, otherwise the input meter. ⚠ Tuning matters more than levels at
    // the moment you are doing it, so it takes the line rather than being squeezed alongside.
    if(g_tuner_on)
    {
        if(g_tune.valid)
            snprintf(buf, sizeof(buf), "TUNE %s%d %+4.0fc",
                     g_tune.name, g_tune.octave, (double)g_tune.cents);
        else
            snprintf(buf, sizeof(buf), "TUNE  -- play --");
    }
    else
    {
        snprintf(buf, sizeof(buf), "by%c tu%c  enc%c",
                 inputs[0].gpio.Read() ? '.' : '#',
                 inputs[1].gpio.Read() ? '.' : '#',
                 g_enc.Pressed() ? '#' : '.');
    }
    OledLine(5, buf);

    // Input level. Read-and-reset gives a true peak for the interval, not a decayed average --
    // a body tap on a piezo is a transient, and an averaged meter would simply miss it.
    float pk = g_peak;
    g_peak   = 0.f;
    if(pk > 0.0002f)
    {
        int db = (int)(20.f * log10f(pk));
        snprintf(buf, sizeof(buf), "in %3ddBFS clip%lu", db, (unsigned long)g_clips);
    }
    else
    {
        snprintf(buf, sizeof(buf), "in   --    clip%lu", (unsigned long)g_clips);
    }
    // ⚠ Gain reduction shares this line rather than getting its own repaint. On the Pico the GR
    // meter defaulted OFF because its extra I2C repaints coupled EMI into the high-Z input
    // ([[project_home_gr_meter_crosstalk]]) -- folding it into the existing 2 Hz refresh adds no
    // bus traffic at all, so it costs nothing to leave on.
    if(!g_dsp_bypass)
    {
        float gr = dsp_chain_comp_gr_db();
        if(gr < -0.2f)
        {
            char g[10];
            snprintf(g, sizeof(g), " gr%-3.0f", (double)gr);
            strncat(buf, g, sizeof(buf) - strlen(buf) - 1);
        }
    }
    OledLine(6, buf);

    snprintf(buf, sizeof(buf), "%s%s ir%s",
             g_dsp_bypass ? "byp " : "ON  ",
             g_preset_count > 0 ? dsp_chain_preset_name(g_preset_idx) : "-",
             g_ir_active ? "*" : "-");
    OledLine(7, buf);

    oled.Update();
}

static bool LoadIr(const char* path)
{
    uint32_t rate = 0, avail = 0;
    int      n    = wav_load_mono_f32(path, g_ir_buf, kIrMaxTaps, &rate, &avail);
    if(n <= 0)
    {
        hw.PrintLine("  IR load FAILED: %s (%s)", path, wav_err_str(n));
        snprintf(g_sd_status, sizeof(g_sd_status), "IR FAILED %s: %s", path, wav_err_str(n));
        return false;
    }

    // ⚠ SAMPLE RATE MUST MATCH END TO END. On the Pico build a mismatched codec rate silently ran
    // the convolver at 2x budget and played a 48 kHz IR an octave high -- and it sounded plausible
    // enough not to be noticed for a while. Refuse rather than play something subtly wrong.
    if(rate != 48000)
    {
        hw.PrintLine("  IR REJECTED: %s is %lu Hz, need 48000", path, (unsigned long)rate);
        snprintf(g_sd_status, sizeof(g_sd_status), "IR REJECTED: %lu Hz not 48k", (unsigned long)rate);
        return false;
    }
    if(avail > (uint32_t)kIrMaxTaps)
        hw.PrintLine("  ⚠ IR truncated: %lu taps available, using %d", (unsigned long)avail, n);

    if(!g_convolver.init(kIrHeadBlock, kIrTailBlock, g_ir_buf, (size_t)n))
    {
        hw.PrintLine("  convolver init FAILED (out of memory?)");
        return false;
    }

    g_ir_len = n;
    snprintf(g_ir_name, sizeof(g_ir_name), "%s", path);
    hw.PrintLine("  IR loaded: %s (%d taps, %lu Hz)", path, n, (unsigned long)rate);
    snprintf(g_sd_status, sizeof(g_sd_status), "IR ok: %d taps", n);
    return true;
}

// Switch preset and load whatever IR it names.
//
// ⚠ NOT audio-safe, and deliberately so: reading a WAV off the SD card blocks for a few hundred
// milliseconds and the convolver is re-initialised underneath a running callback. The Pico build
// records the same "multi-100 ms dropout" on an IR switch. That is acceptable at a deliberate
// preset change and unacceptable anywhere else -- never call this from the audio callback.
//
// ⚠ g_ir_active is cleared FIRST so the callback stops touching the convolver before it is
// re-initialised, with a short delay to let any in-flight block finish.
static void SelectPreset(int idx)
{
    if(g_preset_count <= 0)
        return;
    if(idx < 0)
        idx = g_preset_count - 1;
    if(idx >= g_preset_count)
        idx = 0;

    g_ir_active = false;
    System::Delay(5);

    dsp_chain_load_preset(idx);
    g_preset_idx = idx;
    g_ir_len     = 0;
    snprintf(g_ir_name, sizeof(g_ir_name), "none");

    const char* ir = dsp_chain_preset_ir(idx);
    if(ir && *ir)
        g_ir_active = LoadIr(ir);

    hw.PrintLine("  >> preset %d/%d \"%s\"  ir %s",
                 idx + 1, g_preset_count, dsp_chain_preset_name(idx),
                 g_ir_active ? g_ir_name : "none");
    snprintf(g_sd_status, sizeof(g_sd_status), "%d preset(s), using \"%s\"",
             g_preset_count, dsp_chain_preset_name(idx));
}

// ---------------------------------------------------------------------------------------------
// app_hooks.h — the bridge menu.cpp calls into. State lives here; the menu only drives it.
static bool g_gr_on   = true;
static bool g_in_menu = false;

extern "C" {

int         app_preset_count(void)      { return g_preset_count; }
const char* app_preset_name(int i)      { return dsp_chain_preset_name(i); }
int         app_preset_current(void)    { return g_preset_idx; }
void        app_preset_load(int i)      { SelectPreset(i); }

// ⚠ IR selection is per-PRESET here, not a free-standing table. The Pico scanned the card into an
// IR list the menu could pick from; this build loads whatever IR the chosen preset names. Reported
// as a single-entry list so the menu has something coherent to show rather than an empty picker.
int         app_ir_count(void)          { return 1; }
const char* app_ir_name(int i)          { (void)i; return g_ir_active ? g_ir_name : "none"; }
int         app_ir_current(void)        { return 0; }
void        app_ir_select(int i)        { (void)i; }

bool        app_gr_enabled(void)        { return g_gr_on; }
void        app_gr_set(bool on)         { g_gr_on = on; }

// ⚠ NO PGA ON THIS BOARD. These exist only because menu.cpp calls them. The Pico drove the
// ES8388's input PGA (0..+24 dB in 3 dB steps); the Seed3's TAC5242 is hardware-strapped with no
// software gain control at all, and the front-end level is set by the analogue daughter instead.
// Reporting 0 dB keeps the menu honest rather than showing a control that does nothing.
int         app_pga_nib(void)           { return 0; }
int         app_pga_db(void)            { return 0; }
void        app_pga_set_nib(int n)      { (void)n; }

} // extern "C"

static void PrintFullReport()
{
    hw.PrintLine("");
    hw.PrintLine("--- Tone Trixter Seed3 bring-up (on demand) ---");
    hw.PrintLine("  [1] board      : %s", g_board_ok ? "Daisy Seed3   PASS" : "WRONG         FAIL");
    hw.PrintLine("  [2] i2c 0x%02X   : %s", tt::kOledI2cAddr,
                 g_i2c_ok ? "FOUND         PASS" : "absent        FAIL");
    hw.PrintLine("  [3] inputs     : bypass%c tuner%c  enc SW%c   (# = pressed)",
                 inputs[0].gpio.Read() ? '.' : '#', inputs[1].gpio.Read() ? '.' : '#',
                 g_enc.Pressed() ? '#' : '.');
    hw.PrintLine("  [4] microSD    : CMD0=0x%02X %s · card-detect %s", g_sd_r1,
                 g_sd_ok ? "PASS" : (g_sd_r1 == 0x00 ? "MISO STUCK LOW" : "no card"),
                 g_sd_cd ? "HIGH" : "LOW");
    hw.PrintLine("  display        : %s", oled_ok ? "initialised" : "not running");
    hw.PrintLine("  [5] audio      : sr=%d blk=%lu cb=%lu clips=%lu dsp=%s",
                 (int)hw.AudioSampleRate(), (unsigned long)g_cb_size,
                 (unsigned long)g_cb_count, (unsigned long)g_clips,
                 g_dsp_bypass ? "BYPASS" : "ENGAGED");
    hw.PrintLine("  [6] SD/preset  : %s", g_sd_status);
    hw.PrintLine("  [6] IR         : %s (%d taps)", g_ir_active ? g_ir_name : "none loaded", g_ir_len);
    hw.PrintLine("  ⚠ audio tests need the 9 V JACK. On USB the rail sits ~4.9 V and the");
    hw.PrintLine("    op-amp daughter is outside its common-mode range.");
    hw.PrintLine("  hold encoder switch 2 s -> DFU, then: make flash");
    hw.PrintLine("-----------------------------------------------");
}

int main(void)
{
    hw.Init();
    // ⚠ StartLog(FALSE) -- do NOT wait for a PC.
    //
    // StartLog(true) blocks until a terminal opens, which means on a 9 V supply with no USB the
    // board hangs here forever and never reaches the diagnostics at all. That made the onboard LED
    // useless as a "did it boot" indicator on battery/adapter power (2026-09-05).
    //
    // The boot report is instead REPEATED periodically in the monitor loop, so a terminal attached
    // at any time still sees the results. Better than blocking: the board always runs.
    hw.StartLog(false);

    // Heartbeat immediately, before anything that could block or fail, so the LED proves the board
    // booted even with no console attached at all.
    for(int i = 0; i < 6; i++)
    {
        hw.SetLed(i % 2 == 0);
        System::Delay(80);
    }
    hw.SetLed(false);

    hw.PrintLine("");
    hw.PrintLine("=============================================");
    hw.PrintLine(" Tone Trixter -- Daisy Seed3 bring-up");
    hw.PrintLine(" built " __DATE__ " " __TIME__);
    hw.PrintLine("=============================================");

    int failures = 0;

    hw.PrintLine("");
    hw.PrintLine("[1] board identity");
    bool board_ok = CheckBoard();
    hw.PrintLine("  %s", board_ok ? "PASS" : "FAIL -- expected Daisy Seed3");
    g_board_ok = board_ok;
    failures += board_ok ? 0 : 1;

    hw.PrintLine("");
    hw.PrintLine("[2] I2C1 bus scan (SCL=D%d SDA=D%d)", tt::kOledScl, tt::kOledSda);
    I2CHandle        i2c;
    I2CHandle::Config cfg;
    cfg.periph         = I2CHandle::Config::Peripheral::I2C_1;
    cfg.speed          = I2CHandle::Config::Speed::I2C_400KHZ;
    cfg.mode           = I2CHandle::Config::Mode::I2C_MASTER;
    cfg.pin_config.scl = tt::P(tt::kOledScl);
    cfg.pin_config.sda = tt::P(tt::kOledSda);

    // ⚠ DRIVE THE OLED'S RESET PIN BEFORE TOUCHING THE BUS.
    //
    // RES (D13) is ACTIVE LOW. Left as an unconfigured input it floats, and the SH1106 comes out of
    // reset -- or does not -- differently on each power-up. That is an intermittent that jiggling
    // cannot reproduce, because it is not mechanical.
    //
    // ⚠ It was masked until 2026-09-05 by StartLog(true), which blocked for SECONDS waiting for a
    // terminal and gave the pin ample time to drift high. Switching to StartLog(false) cut that to
    // ~480 ms and the intermittent appeared. The delay was never the fix -- it was the disguise.
    //
    // Assert reset, hold, release, then let the controller come up before addressing it. The pin is
    // left DRIVEN HIGH afterwards, never floating.
    static GPIO oled_res;
    oled_res.Init(tt::P(tt::kOledRes), GPIO::Mode::OUTPUT);
    oled_res.Write(false);
    System::Delay(10);
    oled_res.Write(true);
    System::Delay(50);

    bool i2c_ok = false;
    if(i2c.Init(cfg) != I2CHandle::Result::OK)
        hw.PrintLine("  I2C init FAILED");
    else
        i2c_ok = ScanI2c(i2c);
    hw.PrintLine("  %s", i2c_ok ? "PASS" : "FAIL");
    g_i2c_ok = i2c_ok;
    failures += i2c_ok ? 0 : 1;

    if(i2c_ok)
    {
        TtOled::Config ocfg;
        ocfg.driver_config.transport_config.i2c_address        = tt::kOledI2cAddr;
        ocfg.driver_config.transport_config.i2c_config.periph  = I2CHandle::Config::Peripheral::I2C_1;
        ocfg.driver_config.transport_config.i2c_config.speed   = I2CHandle::Config::Speed::I2C_400KHZ;
        ocfg.driver_config.transport_config.i2c_config.mode    = I2CHandle::Config::Mode::I2C_MASTER;
        ocfg.driver_config.transport_config.i2c_config.pin_config.scl = tt::P(tt::kOledScl);
        ocfg.driver_config.transport_config.i2c_config.pin_config.sda = tt::P(tt::kOledSda);
        oled.Init(ocfg);
        oled.Fill(false);
        oled.Update();
        oled_ok = true;
        tt_oled_shim_bind(&oled);
        menu_init();
        hw.PrintLine("  display initialised");
    }

    hw.PrintLine("");
    hw.PrintLine("[3] digital inputs -- all active LOW, internal pull-ups");

    for(size_t i = 0; i < kNumInputs; i++)
        inputs[i].gpio.Init(tt::P(inputs[i].pin), GPIO::Mode::INPUT, GPIO::Pull::PULLUP);

    g_enc.Init(tt::P(tt::kEncA), tt::P(tt::kEncB), tt::P(tt::kEncSw));
    tuner_init(48000.f);

    // ⚠ Init ALL pins, then settle, THEN read. Reading straight after Init() returns the pin's
    // pre-pull-up state and every input reports a false LOW -- observed on the first hardware run
    // 2026-09-05, where all five read LOW here and then immediately reported "released" in the
    // monitor loop below. The pins were not even wired yet, so a short was impossible: the
    // diagnostic was lying about the board, which is precisely what it exists to prevent.
    System::Delay(10);

    for(size_t i = 0; i < kNumInputs; i++)
    {
        inputs[i].last = inputs[i].gpio.Read();
        hw.PrintLine("  D%-2d %-14s idle=%s",
                     inputs[i].pin,
                     inputs[i].name,
                     inputs[i].last ? "HIGH (ok)" : "LOW  <- shorted to ground, or held down");
    }
    hw.PrintLine("  (HIGH is also what an UNCONNECTED pin reads. Until the encoder and");
    hw.PrintLine("   footswitches are wired this proves the pull-ups, not the wiring.)");

    hw.PrintLine("");
    hw.PrintLine("[4] microSD -- bit-banged SPI, CMD0");
    bool sd_ok = TestSd();
    hw.PrintLine("  %s", sd_ok ? "PASS" : "FAIL (expected until the card + module are in)");
    g_sd_ok = sd_ok;
    failures += sd_ok ? 0 : 1;

    hw.PrintLine("");
    hw.PrintLine("[5] audio passthrough");
    // ⚠ Do NOT call SetAudioSampleRate here. 48 kHz is already libDaisy's default, the working
    // passthrough never called it, and the seed3-updates branch modifies sai.h -- so it is a new
    // variable touching the SAI for no benefit. Removed 2026-09-05 while chasing silent output.
    hw.SetAudioBlockSize(kBlock);

    // ⚠ Unity output level and global bypass ON: complete passthrough at boot, by design.
    dsp_chain_init(48000.f, 1.0f);
    g_dsp_bypass = true;

    hw.StartAudio(AudioCb);
    // Report what the hardware ACTUALLY runs at, rather than what we asked for. If these disagree
    // with 48000 / kBlock, that is the bug rather than anything in the chain.
    hw.PrintLine("  actual: %d Hz, block %u, IN 1 -> OUT 1+2",
                 (int)hw.AudioSampleRate(), (unsigned)hw.AudioBlockSize());
    hw.PrintLine("  DSP chain loaded, GLOBAL BYPASS ON -- pure passthrough");
    hw.PrintLine("  IR: none (loads from SD when a preset asks)");
    hw.PrintLine("  ⚠ 0 dBFS is the CODEC ceiling (1.8 V pk), not the buffer's limit");

    hw.PrintLine("");
    hw.PrintLine("[6] SD card: presets and IR");
    if(FATFS_LinkDriver(&tt_sd_driver, g_sd_path) != 0)
    {
        hw.PrintLine("  FATFS_LinkDriver failed");
        snprintf(g_sd_status, sizeof(g_sd_status), "LinkDriver failed");
    }
    else if(f_mount(&g_fs, g_sd_path, 1) != FR_OK)
    {
        hw.PrintLine("  no filesystem (card absent or not FAT?) -- passthrough only");
        snprintf(g_sd_status, sizeof(g_sd_status), "f_mount FAILED (not FAT / read error)");
    }
    else
    {
        g_sd_mounted = true;
        hw.PrintLine("  mounted at \"%s\"", g_sd_path);

        if(tt_store_load())
        {
            const Preset* pr = tt_store_presets(&g_preset_count);
            hw.PrintLine("  %d preset(s) from the card", g_preset_count);

            // ⚠ INSTALL them into the chain. Reading presets is not the same as installing them:
            // dsp_chain_find_preset() searches the chain's OWN table, so without this it is still
            // looking at the built-ins and returns -1 for every name on the card.
            if(pr && g_preset_count > 0)
                dsp_chain_install_presets(pr, g_preset_count);

            // Pick the boot preset, falling back to the FIRST one if the name is not found --
            // a config naming a preset that does not exist should not mean no preset at all.
            const char* boot = tt_store_boot_preset();
            int         idx  = (boot && *boot) ? dsp_chain_find_preset(boot) : 0;
            if(idx < 0)
            {
                hw.PrintLine("  boot_preset \"%s\" not found -- using the first", boot);
                idx = 0;
            }

            if(g_preset_count > 0)
            {
                dsp_chain_load_preset(idx);
                g_preset_idx = idx;
                hw.PrintLine("  preset: %s", dsp_chain_preset_name(idx));
                snprintf(g_sd_status, sizeof(g_sd_status), "%d preset(s), using \"%s\"",
                         g_preset_count, dsp_chain_preset_name(idx));

                // ⚠ Loading a preset sets stage enables, but global bypass still overrides them --
                // boot stays passthrough by design. The footswitch is what engages it.
                const char* ir = dsp_chain_preset_ir(idx);
                if(ir && *ir)
                    g_ir_active = LoadIr(ir);
                else
                    hw.PrintLine("  preset names no IR");
            }
        }
        else
        {
            hw.PrintLine("  no presets file -- built-in defaults");
            snprintf(g_sd_status, sizeof(g_sd_status), "mounted, NO /tonetrix/presets.txt");
        }
    }
    hw.PrintLine("  ⚠ chain stays BYPASSED at boot. Bypass footswitch toggles it.");

    hw.PrintLine("");
    hw.PrintLine("---------------------------------------------");
    if(failures == 0)
        hw.PrintLine(" ALL AUTOMATED CHECKS PASSED");
    else
        hw.PrintLine(" %d CHECK(S) FAILED -- see above", failures);
    hw.PrintLine("---------------------------------------------");
    hw.PrintLine("");
    hw.PrintLine("Live input monitor. Turn the encoder, press the switches.");
    hw.PrintLine("Onboard LED blinks once a second while this loop runs.");
    hw.PrintLine("");

    // ⚠ The encoder is quadrature: one detent produces transitions on BOTH A and B. Seeing only
    // one of them move is the classic signature of a single bad joint, which is exactly why these
    // are reported as raw edges rather than decoded counts at this stage.
    uint32_t last_blink  = System::GetNow();
    uint32_t last_report = last_blink;
    bool     led         = false;

    while(true)
    {
        for(size_t i = 0; i < kNumInputs; i++)
        {
            bool now = inputs[i].gpio.Read();
            if(now != inputs[i].last)
            {
                hw.PrintLine("  %-14s %s", inputs[i].name, now ? "released" : "PRESSED");
                inputs[i].last = now;

                // ⚠ The bypass footswitch toggles the chain on its PRESS edge. Interim control
                // until there is a menu -- and it is the A/B this whole project turns on: the same
                // playing, with and without the DSP, switched instantly.
                if(i == 0 && !now)          // bypass footswitch
                {
                    g_dsp_bypass = !g_dsp_bypass;
                    hw.PrintLine("  >> DSP %s%s",
                                 g_dsp_bypass ? "BYPASSED" : "ENGAGED",
                                 (!g_dsp_bypass && g_ir_active) ? " (with IR)" : "");
                }
                if(i == 1 && !now)          // tuner footswitch -- its actual job now
                {
                    g_tuner_on = !g_tuner_on;
                    hw.PrintLine("  >> TUNER %s", g_tuner_on ? "ON" : "off");
                }
            }
        }

        uint32_t t = System::GetNow();

        // ⚠ Re-print the verdict every 5 s. StartLog(false) means the board no longer waits for a
        // terminal, so the boot report may already have scrolled past -- or never been seen at all
        // if the board was powered from the 9 V jack. This makes the state observable whenever a
        // console is attached, instead of only in the first second after reset.
        if(t - last_report >= 5000)
        {
            last_report = t;
            // ⚠ THIS LINE IS THE REAL REPORT, not a convenience.
            //
            // StartLog(false) means the board prints its boot report immediately -- BEFORE the USB
            // CDC device has enumerated on the host -- so that output is routinely lost or garbled
            // (seen as "===========$$" 2026-09-05). Anything a person needs to read must therefore
            // appear HERE, in a line that repeats. Adding a check above without adding it here
            // makes it invisible in practice.
            hw.PrintLine("[%lus] board=%s · i2c 0x%02X=%s · sd CMD0=0x%02X %s · %s",
                         (unsigned long)(t / 1000),
                         board_ok ? "Seed3 ok" : "WRONG",
                         tt::kOledI2cAddr,
                         i2c_ok ? "FOUND" : "absent",
                         g_sd_r1,
                         sd_ok ? "ok" : (g_sd_r1 == 0x00 ? "MISO-LOW!" : "no-card"),
                         g_sd_cd ? "cd=HIGH" : "cd=LOW");
            hw.PrintLine("        audio: sr=%d blk=%lu cb=%lu clips=%lu dsp=%s",
                         (int)hw.AudioSampleRate(),
                         (unsigned long)g_cb_size,
                         (unsigned long)g_cb_count,
                         (unsigned long)g_clips,
                         g_dsp_bypass ? "BYPASS" : "on");

        }

        // ---- HOLD THE ENCODER SWITCH FOR 2 s TO ENTER DFU ----
        //
        // ⚠ This is not just convenience. The Daisy is UNDER-MOUNTED on the main board, so its BOOT
        // and RESET buttons face the perfboard and may be unreachable -- in which case this is the
        // only way to flash without unseating the module.
        //
        // A 2 s hold with a visible countdown, on a control nothing else uses at this stage, so it
        // cannot fire by accident. Jumps to the STM32 ROM bootloader; `make program-dfu` then works
        // with no button-press dance at all.
        g_enc.Debounce();

        // ⚠ Rotation selects presets. This is what the encoder is FOR, and it frees the tuner
        // footswitch to do its actual job -- cycling presets on a footswitch was always a stopgap.
        int32_t inc = g_enc.Increment();
        if(inc != 0 && !g_in_menu && g_preset_count > 0)
            SelectPreset(g_preset_idx + (inc > 0 ? 1 : -1));
        else if(inc != 0 && g_in_menu)
            menu_event((int)inc, false);

        static uint32_t sw_down_at = 0;
        bool            sw_held    = g_enc.Pressed();
        if(sw_held)
        {
            if(sw_down_at == 0)
                sw_down_at = t;
            uint32_t held = t - sw_down_at;
            if(held >= 2000)
            {
                if(oled_ok)
                {
                    oled.Fill(false);
                    OledLine(2, "  ENTERING DFU");
                    OledLine(4, "  flash now:");
                    OledLine(5, "  make program-dfu");
                    oled.Update();
                }
                hw.PrintLine("Entering DFU bootloader -- run: make program-dfu");
                System::Delay(400);
                // ⚠ DAISY, not STM. With APP_TYPE = BOOT_SRAM the app is loaded by the Daisy
                // bootloader; jumping to the STM ROM bootloader would bypass it.
                System::ResetToBootloader(System::BootloaderMode::DAISY);
            }
            else if(oled_ok && held > 400)
            {
                char b[24];
                snprintf(b, sizeof(b), "  DFU in %lu.%lus",
                         (unsigned long)((2000 - held) / 1000),
                         (unsigned long)(((2000 - held) % 1000) / 100));
                oled.Fill(false);
                OledLine(3, b);
                OledLine(5, "  release to cancel");
                oled.Update();
                System::Delay(60);
                continue;                       // hold the countdown on screen
            }
        }
        else
        {
            // Short press (< 2 s): open the menu from home, or select within it.
            //
            // ⚠ This replaces the on-demand report, which now has nowhere to be triggered from. The
            // repeating 5 s summary line carries every check, which is why it was made comprehensive
            // -- the report was a convenience on top of it, not the only route to the information.
            if(sw_down_at != 0 && (t - sw_down_at) < 2000)
            {
                // ⚠ Print the report on every menu transition. It has to stay reachable somehow --
                // it was the output that diagnosed the SD/preset and audio faults -- and a menu
                // open/close is a deliberate act, so it never spams the log.
                PrintFullReport();
                if(!g_in_menu)
                {
                    menu_open();
                    g_in_menu = true;
                }
                else
                {
                    menu_event(0, true);
                    if(menu_take_home())
                        g_in_menu = false;
                }
            }
            sw_down_at = 0;
        }

        // Refresh the screen twice a second -- fast enough to feel live when pressing a switch,
        // slow enough that the I2C traffic is not sitting on top of a high-Z audio input.
        static uint32_t last_oled = 0;
        // ⚠ The menu redraws FASTER than the home screen (100 ms vs 500 ms). Navigation has to feel
        // immediate under the fingers; the home screen is glanceable data that does not.
        if(g_in_menu)
        {
            if(t - last_oled >= 100)
            {
                last_oled = t;
                menu_render();
                oled_flush();
            }
        }
        else if(t - last_oled >= 500)
        {
            last_oled = t;
            OledReport(board_ok, i2c_ok, sd_ok, g_sd_r1, g_sd_cd, t / 1000);
        }

        if(t - last_blink >= 1000)
        {
            last_blink = t;
            led        = !led;
            hw.SetLed(led);
        }

        // ⚠ BUSY-WAIT, DELIBERATELY -- and it makes the STM32H750 run hot. See below.
        //
        // System::Delay() is HAL_Delay(), which spins at full clock. Polling five GPIOs does not
        // need a 400 MHz Cortex-M7 flat out, and on 2026-09-05 this made the chip painful to touch
        // (bootloader, by comparison, is cool -- so it is the application, not the board).
        //
        // ⚠ __WFI() FIXES THE HEAT BUT BREAKS THE USB CONSOLE. Tried 2026-09-05: the chip dropped
        // to slightly warm, but `screen` disconnected the instant this loop was reached, every
        // time. libDaisy's USB suspend callback sets SLEEPDEEP and SLEEPONEXIT
        // (src/usbd/usbd_conf.c:314), which would turn __WFI() into STOP mode and gate the USB
        // clocks -- but clearing both bits immediately before __WFI() did NOT fix it, so that is
        // not the whole story and the real cause is still unidentified.
        //
        // Accepting the heat for now: during bring-up a diagnostic that disconnects is worse than
        // one that runs warm.
        //
        // ⚠ REVISIT once the OLED is working. The serial console is only the primary interface
        // while there is no display; report to the screen instead and __WFI() becomes viable, which
        // matters because the DSP chain will spend the thermal headroom this would have bought.
        System::Delay(1);
    }
}
