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
#include "audio/backing.h"
extern "C" {
#include "ff.h"
#include "ff_gen_drv.h"
extern const Diskio_drvTypeDef tt_sd_driver;
}
#include "TwoStageFFTConvolver.h"

#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdlib>

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
static bool oled_ok = false;

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
static bool          g_gr_on    = true;    // GR band on the home screen
static bool          g_in_menu  = false;
static bool          g_oled_dirty = true;

// ⚠ TIMING INSTRUMENTATION. Several rounds of guessing at "the menu is stuck" produced fixes that
// each helped a little and none of which settled it. These record the WORST case seen for each
// stage of the foreground loop, so the next report says which one actually costs the time instead
// of us both reasoning about it. Reset with the `times` command.
static volatile uint32_t g_t_flush   = 0;   // us, worst oled_flush()
static volatile uint32_t g_t_render  = 0;   // us, worst menu_render()
static volatile uint32_t g_t_service = 0;   // us, worst backing_service()
static volatile uint32_t g_t_loop    = 0;   // us, worst whole iteration (work only)
// ⚠ PERIOD between iteration starts -- catches anything the work timer cannot, including
// System::Delay() and any stall between passes. If a hang shows here but not in g_t_loop, the time
// is going somewhere outside the instrumented work.
static volatile uint32_t g_t_period  = 0;   // ⚠ MILLISECONDS, not us -- see below
static volatile uint32_t g_t_wraps   = 0;   // discarded samples, evidence of the wrap

// ⚠ ENCODER INSTRUMENTATION. Timing is measurably fine (max period 26 ms) yet navigation is hard,
// so the next suspect is the INPUT rather than the loop: if one physical detent yields several
// counts, the menu jumps unpredictably no matter how fast the display is. `enc on` prints every
// increment as it is banked, so one click of the knob can be compared against what the code sees.
static volatile bool     g_enc_dbg   = false;
static volatile int32_t  g_enc_total = 0;
static volatile uint32_t g_enc_evts  = 0;
static volatile uint8_t  g_enc_raw  = 3;   // last raw (A<<1)|B, for the `enc` report
// ⚠ RAW PIN INSTRUMENTATION. Two decode fixes in a row each looked right and each left detents
// missing, so stop reasoning about the decode and measure the PINS. These separate three questions
// that have been running together:
//   tick=  did the sampler actually run, and at what rate  (10 kHz => ~50000 per 5 s)
//   trans= did the pins CHANGE STATE at all while the knob was turning
//   seen=  which of the four A/B states have EVER been observed
// trans=0 while turning is a joint or a pin, not firmware. trans large with cnt=0 is my logic.
// seen missing a state says one of A/B never moves -- which is a wiring answer, not a code one.
static volatile uint32_t g_enc_ticks = 0;
static volatile uint32_t g_enc_trans = 0;
static volatile uint8_t  g_enc_seen  = 0;   // bit N set = state N observed

// ⚠ Quarter steps per detent. A PEC11R with 24 detents / 24 pulses does a FULL cycle per click (4),
// but half-cycle variants exist that rest alternately at 11 and 00 -- on those, a 4 rule emits on
// every OTHER click, which looks exactly like "missing many". Runtime-settable with `encdet` so the
// question is answered on the bench in seconds instead of by another rebuild.
static volatile int8_t g_q_per_detent = 4;

// ⚠ LIVE PIN WATCH, for hunting a dry joint. `encwatch on` prints A/B whenever the pins change, so
// wiggling a suspect leg with the knob held still says immediately whether that pin is connected.
// Rate-limited in the foreground: at 10 kHz a bouncing contact would otherwise flood the console
// and the flood itself would hide the answer.
static volatile bool g_enc_watch = false;

// ⚠ OUR OWN QUADRATURE DECODE -- libDaisy's Encoder::Increment() cannot decode this encoder.
//
// libDaisy (src/hid/encoder.cpp) emits a step on ONE exact sample pattern:
//     A's last two samples == 0b10  AND  B's last two samples == 0b00
// -- a falling edge on A at the instant B has already read low for two consecutive samples. A
// PEC11R rests with both contacts OPEN (1,1) at every detent and runs
//     1,1 -> 0,1 -> 0,0 -> 1,0 -> 1,1
// through one click, so that pattern exists in exactly ONE of the four transitions, and only if the
// sampler happens to catch the intermediate states in that order. Turn slowly and contact bounce
// breaks the exact match; turn quickly and the states pass between samples. A narrow band of speeds
// in the middle works and nothing else does -- which is exactly the reported symptom, "have to turn
// at a very specific rate" (2026-09-06). Four rounds of display and timing fixes could not have
// helped, because the counts were never being produced.
//
// This is a full state-machine decode instead: every one of the four transitions is a quarter step
// and one detent is emitted per completed cycle. Contact bounce inside a cycle nets to zero (a
// reversal subtracts exactly what it added), so no debounce delay is needed and no speed is special.
static const int8_t kQuadStep[16] = {
    // index = (prev << 2) | cur, where state = (A << 1) | B
     0, -1, +1,  0,
    +1,  0,  0, -1,
    -1,  0,  0, +1,
     0, +1, -1,  0,
};

static GPIO    g_enc_a, g_enc_b;
static uint8_t g_q_prev = 0x03;
static uint8_t g_q_rest = 0x03;   // the state the knob sits in between detents, sampled at Init
static int8_t  g_q_sub  = 0;      // quarter steps accumulated within the current cycle
static int8_t  g_q_dir  = 1;      // last unambiguous direction, for skipped-state recovery

static int32_t EncoderPoll()
{
    const uint8_t cur = (uint8_t)((g_enc_a.Read() ? 2 : 0) | (g_enc_b.Read() ? 1 : 0));
    g_enc_raw         = cur;
    g_enc_ticks++;
    g_enc_seen |= (uint8_t)(1u << cur);
    if(cur == g_q_prev)
        return 0;
    g_enc_trans++;

    int8_t step = kQuadStep[(g_q_prev << 2) | cur];
    if(step == 0)
    {
        // ⚠ Both bits changed between samples, so a state was skipped -- unavoidable at any finite
        // sample rate on a fast turn. Direction is genuinely ambiguous here, so carry on in the
        // direction already established rather than discarding the movement. Discarding it is what
        // makes a fast spin do nothing at all.
        step = (int8_t)(2 * g_q_dir);
    }
    else
    {
        g_q_dir = (step > 0) ? 1 : -1;
    }
    g_q_sub  = (int8_t)(g_q_sub + step);
    g_q_prev = cur;

    // ⚠ A complete detent's worth of quarter steps, wherever it happens to land.
    if(g_q_sub >= g_q_per_detent)  { g_q_sub = 0; return +1; }
    if(g_q_sub <= -g_q_per_detent) { g_q_sub = 0; return -1; }

    // ⚠ Otherwise emit only back at the detent rest position: the click is complete when the
    // contacts are open again. Partial nudges that spring back produce nothing, and because the
    // accumulator is cleared here one detent can never produce two counts.
    if(cur != g_q_rest)
        return 0;

    int32_t out = 0;
    if(g_q_sub >= 2)
        out = +1;
    else if(g_q_sub <= -2)
        out = -1;
    g_q_sub = 0;
    return out;
}

// ⚠ Preset changes from the HOME screen are DEFERRED until the encoder settles.
//
// SelectPreset() reads a WAV off the SD card and re-initialises the convolver -- hundreds of
// milliseconds. Doing that on every detent made the pedal appear to hang for seconds when spinning
// through presets, and got noticeably worse the moment IR loading actually started working
// (2026-09-06): before that the load failed fast, so the cost was invisible.
//
// Turning now moves an index and repaints immediately; the load fires once the encoder has been
// still for kPresetSettleMs. Spin freely, and it commits where you stop.
static constexpr uint32_t kPresetSettleMs = 400;
static int      g_preset_pending = -1;      // -1 = nothing waiting
static uint32_t g_preset_moved_at = 0;

// ⚠ The encoder is polled from the AUDIO CALLBACK, at a rock-steady 750 Hz.
//
// Polling it from the foreground loop meant an irregular ~300 Hz -- backing_service() alone may
// spend 2 ms, and an OLED flush far more -- and libDaisy's Encoder needs consistent sampling to
// decode quadrature. That irregularity is what made turns feel laggy and drop detents (2026-09-06).
// Debounce() is only a few GPIO reads and shifts, which is nothing against a 64-sample block.
//
// Increments accumulate here so none are lost between foreground passes; the loop drains it.
// The SWITCH is still read as a level in the foreground -- the press/hold distinction needs
// duration, not an edge, and Pressed() is now debounced at the callback's steady rate too.
static volatile int32_t g_enc_delta = 0;

// ⚠ THE ENCODER IS SAMPLED BY A 10 kHz TIMER, NOT BY THE AUDIO CALLBACK.
//
// The callback's 750 Hz is a 1.33 ms sample period, and a brisk flick of the wrist takes a PEC11R
// through a whole 11 -> 01 -> 00 -> 10 -> 11 cycle in less than that. When every intermediate state
// falls between two samples the detent is invisible: no counts, no evidence, nothing to decode.
// Measured 2026-09-06 -- with the decode finally correct (one count per detent, right direction,
// no over-counting) whole clicks still produced no line at all.
//
// TIM4 at 10 kHz gives 13 samples across a 1.33 ms window instead of one. It is unused by libDaisy
// (only PWMHandle touches TIM3/4/5) and costs a handful of GPIO reads per tick.
static TimerHandle g_enc_tim;

static void EncTick(void*)
{
    int32_t d = EncoderPoll();
    if(d != 0)
    {
        g_enc_delta += d;
        g_enc_total += d;
        g_enc_evts++;
    }
}

// ---------------------------------------------------------------------------------------------
// Terminal command interface.
//
// ⚠ Restores the UART commands from the RP2350 build. dsp_chain_command() was already ported and
// handles every stage/param command -- it only ever needed a line of text delivered to it. Commands
// beat the encoder for anything diagnostic: the output goes to the terminal anyway, so having the
// request there too keeps question and answer in one place, and a keyboard beats a rotary encoder
// for typing a parameter value.
//
// The USB RX callback runs in interrupt context, so it does the minimum -- copy bytes into a line
// buffer and set a flag. Everything else happens in the foreground.
static char           g_cmd[96];
static volatile int   g_cmd_len   = 0;
static volatile bool  g_cmd_ready = false;

static void UsbRx(uint8_t* buf, uint32_t* len)
{
    for(uint32_t i = 0; i < *len; i++)
    {
        char c = (char)buf[i];
        if(c == '\r' || c == '\n')
        {
            if(g_cmd_len > 0)
            {
                g_cmd[g_cmd_len] = 0;
                g_cmd_ready      = true;
            }
        }
        else if(g_cmd_len < (int)sizeof(g_cmd) - 1 && !g_cmd_ready)
        {
            g_cmd[g_cmd_len++] = c;
        }
    }
}

// ⚠ TUNER RUNS IN THE FOREGROUND, NOT THE CALLBACK. Its autocorrelation is far too heavy for a
// 64-sample audio block -- running it inline caused heavy clipping and dropouts (2026-09-06). The
// callback only COPIES samples here, which is cheap; the foreground drains and analyses them.
// ⚠ A RING, not a fill-and-stop buffer. The first attempt stopped collecting once full and
// resumed only after the foreground drained it, so the tuner saw disjoint chunks with gaps --
// and autocorrelation needs CONTIGUOUS samples to find a period. That is why it engaged but never
// reported a note (2026-09-06), and why a backing track made it worse: more foreground work meant
// longer gaps.
static constexpr int    kTunRing = 4096;
static float            g_tun_buf[kTunRing];
static volatile int     g_tun_w  = 0;      // callback writes here
static int              g_tun_r  = 0;      // foreground reads here
static volatile bool    g_tun_ovf = false;
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

// ⚠ SEPARATE convolver output. TwoStageFFTConvolver::process(input, output, len) reads its input
// while writing its output -- passing the same buffer for both aliases them and the signal comes
// out silent. The Pico always used two buffers (s_dsp_in -> s_dsp_out); calling it in-place here
// was my error, and it only showed once an IR actually loaded and the convolver first ran.
static float            g_conv[kWorkMax];

// What the callback ACTUALLY receives, surfaced in the repeating summary line. ⚠ The boot report is
// routinely lost to USB CDC enumeration, so a number that only prints at boot is a number nobody
// reads -- which is how the block size stayed unverified while we guessed at it.
static volatile uint32_t g_cb_size = 0;
static volatile uint32_t g_cb_count = 0;

static volatile float    g_peak  = 0.f;   // peak |sample| since the display last read it
static volatile uint32_t g_clips     = 0;
static volatile bool     g_clip_recent = false;   // sticky until the splash shows it

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
        {
            g_clips++;
            g_clip_recent = true;
        }
        g_work[i] = in[0][i];
    }
    if(pk > g_peak)
        g_peak = pk;

    // Encoder, at the callback's steady rate -- see g_enc_delta.
    // ⚠ The SWITCH only. A/B are sampled by TIM4 at 10 kHz -- see EncTick().
    g_enc.Debounce();

    // Copy for the tuner -- no analysis here. See g_tun_buf.
    if(g_tuner_on)
    {
        int w = g_tun_w;
        for(size_t i = 0; i < size; i++)
        {
            g_tun_buf[w] = g_work[i];
            w            = (w + 1) & (kTunRing - 1);
        }
        g_tun_w = w;
    }

    // IR first, then the chain -- same order as the Pico build.
    if(g_ir_active && dsp_chain_ir_enabled())
    {
        g_convolver.process(g_work, g_conv, size);      // ⚠ NOT in-place -- see g_conv
        for(size_t i = 0; i < size; i++)
            g_work[i] = g_conv[i];
    }

    dsp_chain_process(g_work, (int)size);   // EQ -> Dynamics -> Output level (all off when bypassed)

    // ⚠ Backing track mixes in AFTER the chain, exactly as on the Pico: it is already-produced
    // audio and must not be run through the IR, EQ or compressor. Putting the bed through the
    // guitar's processing would be both wrong and a waste of the budget.
    backing_mix(g_work, (int)size);

    // ⚠ TUNER MUTES THE OUTPUT, backing track included. That is what a tuner pedal does, and it is
    // what makes it usable on stage. Detection is unaffected: the tuner reads the INPUT, upstream of
    // everything here.
    if(g_tuner_on)
        for(size_t i = 0; i < size; i++)
            g_work[i] = 0.f;

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
    oled_text(0, row * 8, text);
}

// Home screen. ⚠ SETTINGS, not diagnostics -- the bring-up checks have served their purpose and
// live in the terminal now. What a player needs at a glance is which preset and IR are live, whether
// the chain is engaged, the input level, and gain reduction.
static void OledHome(void)
{
    if(!oled_ok)
        return;

    char buf[26];
    oled_clear();

    // ⚠ EVERY ELEMENT SITS ON AN 8-PIXEL PAGE BOUNDARY (y = 0, 16, 24, 40, 48, 56).
    //
    // The panel writes in 8-row pages, and oled_flush() sends only pages whose content changed. Text
    // at y=12 straddles pages 1 AND 2, so a single changing row dirties two pages and doubles the
    // flush cost. Aligning the layout means a moving meter dirties exactly the page it lives in --
    // measured 21-24 ms before, and it is the meters that change on every refresh.

    // --- what is SET ---------------------------------------------------------------------------
    oled_text(0, 0, g_dsp_bypass ? "Tone Trixter  BYP" : "Tone Trixter   ON");

    // Show the PENDING preset while the encoder is still moving -- the name must track the knob
    // even though the load has not happened yet, or turning feels dead.
    const int shown = (g_preset_pending >= 0) ? g_preset_pending : g_preset_idx;
    snprintf(buf, sizeof(buf), "P  %s%s", g_preset_count > 0 ? dsp_chain_preset_name(shown) : "-",
             g_preset_pending >= 0 ? " ..." : "");
    oled_text(0, 16, buf);

    // ⚠ Show the preset's IR NAME even when it is not loaded, with a marker for which. Reporting a
    // bare "none" for a preset that names an IR made a failed LOAD look like a preset with no IR
    // (2026-09-06) -- two very different faults that need telling apart.
    {
        const char* ir = (g_preset_count > 0) ? dsp_chain_preset_ir(shown) : "";
        if(ir && *ir)
        {
            const char* base = strrchr(ir, '/');
            base = base ? base + 1 : ir;
            snprintf(buf, sizeof(buf), "IR %c%s", g_ir_active ? '*' : '!', base);
        }
        else
        {
            snprintf(buf, sizeof(buf), "IR  none");
        }
        oled_text(0, 24, buf);
    }

    // --- meters: bar + written value ---------------------------------------------------------
    float pk = g_peak;
    g_peak   = 0.f;
    int   dbfs = (pk > 0.0002f) ? (int)(20.f * log10f(pk)) : -99;

    oled_text(0, 40, "in");
    oled_bar(18, 40, 62, 8, pk);                       // linear: matches how a peak FEELS
    snprintf(buf, sizeof(buf), "%4d", dbfs);
    oled_text(84, 40, dbfs > -99 ? buf : "  --");

    // ⚠ Clip is STICKY until shown, not sampled. A single clipped sample between two 500 ms
    // refreshes would otherwise never be seen -- and a transient is exactly what clips here.
    if(g_clip_recent)
    {
        oled_text(110, 40, "C");
        g_clip_recent = false;
    }

    float gr = (!g_dsp_bypass && g_gr_on) ? dsp_chain_comp_gr_db() : 0.f;
    oled_text(0, 48, "gr");
    oled_bar(18, 48, 62, 8, (-gr) / 20.f);             // 0..-20 dB of reduction across the bar
    snprintf(buf, sizeof(buf), "%4.1f", (double)(gr < -0.05f ? gr : 0.f));
    oled_text(84, 48, buf);

    if(g_tuner_on)
    {
        if(g_tune.valid)
            snprintf(buf, sizeof(buf), "TUNE %s%d %+4.0fc",
                     g_tune.name, g_tune.octave, (double)g_tune.cents);
        else
            snprintf(buf, sizeof(buf), "TUNE listening...");
        oled_text(0, 56, buf);
    }

    oled_flush();
}

static bool LoadIr(const char* name)
{
    // ⚠ RESOLVE THE NAME TO A PATH. presets.txt names an IR as a bare filename
    // ("tanglewood.wav"), and the card keeps them in /tonetrix/ir/. Passing the bare name to the
    // file opener looked in the ROOT and failed every time (2026-09-06) -- the preset was fine, the
    // file was fine, only the path was missing. An absolute name is used as-is so a preset can
    // still point anywhere on the card.
    char path[64];
    if(name && name[0] == '/')
        snprintf(path, sizeof(path), "%s", name);
    else
        snprintf(path, sizeof(path), "/tonetrix/ir/%s", name ? name : "");

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

    // ⚠ bk_level of 0 means "not specified", not "silence". The built-in preset table predates this
    // field and zero-initialises it, so treating 0 as a real level would mute the backing track on
    // every preset that has not opted in.
    int         np = 0;
    const Preset* pl = tt_store_presets(&np);
    if(pl && idx < np && pl[idx].bk_level > 0.f)
        backing_set_level(pl[idx].bk_level);

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


extern "C" {

int         app_preset_count(void)      { return g_preset_count; }
const char* app_preset_name(int i)      { return dsp_chain_preset_name(i); }
int         app_preset_current(void)    { return g_preset_idx; }
void        app_preset_load(int i)      { SelectPreset(i); }

// ⚠ IR selection is per-PRESET here, not a free-standing table. The Pico scanned the card into an
// IR list the menu could pick from; this build loads whatever IR the chosen preset names. Reported
// as a single-entry list so the menu has something coherent to show rather than an empty picker.
int         app_ir_count(void)          { return 1; }
const char* app_ir_name(int i)
{
    // ⚠ Reports the IR of the CURRENT preset, live. Returning a cached name meant the menu's IR row
    // never changed when the preset did (2026-09-06) -- it looked like preset loading was broken
    // when in fact only the display was stale.
    (void)i;
    const char* ir = dsp_chain_preset_ir(g_preset_idx);
    return (ir && *ir) ? ir : "none";
}
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

static void PrintFullReport();

static void HandleCommand(const char* line)
{
    if(!line || !*line)
        return;

    if(strcmp(line, "help") == 0)
    {
        hw.PrintLine("commands:");
        hw.PrintLine("  status            board + audio + SD report");
        hw.PrintLine("  presets           list presets");
        hw.PrintLine("  preset <n|name>   select preset (loads its IR)");
        hw.PrintLine("  bypass on|off     chain bypass");
        hw.PrintLine("  tuner on|off      tuner");
        hw.PrintLine("  gr on|off         GR band on the home screen");
        hw.PrintLine("  bk                list backing tracks");
        hw.PrintLine("  bk <n>|off        play / stop a backing track");
        hw.PrintLine("  bklevel <0..2>    backing level");
        hw.PrintLine("  dfu               reboot to the bootloader");
        hw.PrintLine("  ---- and every dsp_chain command ----");
        dsp_chain_command("?");
        return;
    }
    if(strcmp(line, "status") == 0)  { PrintFullReport(); return; }
    if(strncmp(line, "enc ", 4) == 0)
    {
        g_enc_dbg = (strcmp(line + 4, "on") == 0);
        hw.PrintLine("enc debug=%s  (turn ONE detent and count the lines)",
                     g_enc_dbg ? "on" : "off");
        g_enc_total = 0;
        g_enc_evts  = 0;
        return;
    }
    if(strncmp(line, "encwatch ", 9) == 0)
    {
        g_enc_watch = (strcmp(line + 9, "on") == 0);
        hw.PrintLine("enc watch=%s -- hold the knob STILL and wiggle each leg in turn",
                     g_enc_watch ? "on" : "off");
        if(g_enc_watch)
            hw.PrintLine("  a leg that is connected toggles its pin; a dry one never moves");
        return;
    }
    if(strncmp(line, "encdet ", 7) == 0)
    {
        int n = atoi(line + 7);
        if(n == 2 || n == 4)
        {
            g_q_per_detent = (int8_t)n;
            g_q_sub        = 0;
            hw.PrintLine("quarter steps per detent = %d", n);
        }
        else
            hw.PrintLine("encdet takes 4 (full cycle per click) or 2 (half cycle)");
        return;
    }
    if(strcmp(line, "enc") == 0)
    {
        hw.PrintLine("encoder: %ld counts in %lu events since last reset",
                     (long)g_enc_total, (unsigned long)g_enc_evts);
        hw.PrintLine("  raw A/B=%d%d  rest=%d%d  sub=%d",
                     (g_enc_raw >> 1) & 1, g_enc_raw & 1,
                     (g_q_rest >> 1) & 1, g_q_rest & 1, (int)g_q_sub);
        hw.PrintLine("  ⚠ one detent should be ONE count.");
        g_enc_total = 0;
        g_enc_evts  = 0;
        return;
    }
    if(strcmp(line, "times") == 0)
    {
        hw.PrintLine("worst-case us: flush=%lu render=%lu service=%lu loop=%lu PERIOD=%lums wraps=%lu",
                     (unsigned long)g_t_flush, (unsigned long)g_t_render,
                     (unsigned long)g_t_service, (unsigned long)g_t_loop,
                     (unsigned long)g_t_period, (unsigned long)g_t_wraps);
        g_t_flush = g_t_render = g_t_service = g_t_loop = g_t_period = g_t_wraps = 0;
        hw.PrintLine("(reset)");
        return;
    }
    if(strcmp(line, "dfu") == 0)
    {
        hw.PrintLine("rebooting to bootloader...");
        System::Delay(200);
        System::ResetToBootloader(System::BootloaderMode::DAISY);
        return;
    }
    if(strcmp(line, "presets") == 0)
    {
        for(int i = 0; i < g_preset_count; i++)
            hw.PrintLine("  %c%d %-12s ir=%s", i == g_preset_idx ? '*' : ' ', i,
                         dsp_chain_preset_name(i), dsp_chain_preset_ir(i));
        return;
    }
    if(strncmp(line, "preset ", 7) == 0)
    {
        const char* a = line + 7;
        int         i = (a[0] >= '0' && a[0] <= '9') ? atoi(a) : dsp_chain_find_preset(a);
        if(i < 0 || i >= g_preset_count)
            hw.PrintLine("? no preset '%s'", a);
        else
        {
            SelectPreset(i);
            g_oled_dirty = true;
        }
        return;
    }
    if(strncmp(line, "tuner ", 6) == 0)
    {
        g_tuner_on   = (strcmp(line + 6, "on") == 0);
        g_oled_dirty = true;
        hw.PrintLine("tuner=%s", g_tuner_on ? "on" : "off");
        return;
    }
    if(strncmp(line, "gr ", 3) == 0)
    {
        g_gr_on      = (strcmp(line + 3, "on") == 0);
        g_oled_dirty = true;
        hw.PrintLine("gr=%s", g_gr_on ? "on" : "off");
        return;
    }
    if(strcmp(line, "bk") == 0)
    {
        hw.PrintLine("  %d track(s), playing=%d", backing_count(), backing_current());
        for(int i = 0; i < backing_count(); i++)
            hw.PrintLine("  %c%d %s", i == backing_current() ? '*' : ' ', i, backing_name(i));
        return;
    }
    if(strncmp(line, "bk ", 3) == 0)
    {
        if(strcmp(line + 3, "off") == 0) { backing_stop(); hw.PrintLine("bk off"); }
        else                             { backing_play(atoi(line + 3)); }
        return;
    }
    if(strncmp(line, "bklevel ", 8) == 0)
    {
        backing_set_level((float)atof(line + 8));
        hw.PrintLine("bklevel=%.2f", (double)backing_level());
        return;
    }

    // ⚠ Everything else goes to dsp_chain_command, which already implements the full stage/param
    // language from the Pico build -- "bypass on", "eq.low 3", "comp off" and so on. It returns
    // false for anything it does not recognise.
    if(!dsp_chain_command(line))
        hw.PrintLine("? '%s' -- try 'help'", line);
    g_oled_dirty = true;
}

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
    hw.usb_handle.SetReceiveCallback(UsbRx, UsbHandle::FS_INTERNAL);

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
        // ⚠ Our own SH1106 driver, not libDaisy's -- see audio/oled_shim.cpp for why (libDaisy
        // sends one I2C transaction PER BYTE, ~77 ms a frame, which starved the whole foreground
        // loop). It borrows the already-configured bus rather than re-initialising it.
        tt_oled_init(i2c, tt::kOledI2cAddr);
        oled_ok = true;
        menu_init();
        hw.PrintLine("  display initialised (batched page writes)");
    }


    hw.PrintLine("");
    hw.PrintLine("[3] digital inputs -- all active LOW, internal pull-ups");

    for(size_t i = 0; i < kNumInputs; i++)
        inputs[i].gpio.Init(tt::P(inputs[i].pin), GPIO::Mode::INPUT, GPIO::Pull::PULLUP);

    g_enc.Init(tt::P(tt::kEncA), tt::P(tt::kEncB), tt::P(tt::kEncSw));

    // ⚠ A and B are read by EncoderPoll(), not by libDaisy's broken decode. g_enc keeps the click.
    // Two GPIO objects on one pin is harmless -- both configure it identically and only read it.
    g_enc_a.Init(tt::P(tt::kEncA), GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    g_enc_b.Init(tt::P(tt::kEncB), GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    System::Delay(2);   // let the pull-ups settle before sampling the rest state
    g_q_rest = (uint8_t)((g_enc_a.Read() ? 2 : 0) | (g_enc_b.Read() ? 1 : 0));
    g_q_prev = g_q_rest;
    hw.PrintLine("  encoder rest state A/B = %d%d", (g_q_rest >> 1) & 1, g_q_rest & 1);

    // ⚠ 10 kHz sampler. Prescaler is 0 out of Init(), so GetFreq() reports the raw 2x-PClk1 tick
    // rate; dividing that down to 1 MHz and reloading every 100 ticks lands on 10 kHz whatever the
    // core clock is, rather than hard-coding a number that a boost-clock change would silently
    // falsify.
    TimerHandle::Config tcfg;
    tcfg.periph     = TimerHandle::Config::Peripheral::TIM_4;
    tcfg.dir        = TimerHandle::Config::CounterDir::UP;
    tcfg.period     = 0xffff;
    tcfg.enable_irq = true;
    if(g_enc_tim.Init(tcfg) == TimerHandle::Result::OK)
    {
        const uint32_t base = g_enc_tim.GetFreq();          // ticks/s with PSC = 0
        g_enc_tim.SetPrescaler((base / 1000000u) - 1u);     // -> 1 MHz
        g_enc_tim.SetPeriod(99);                            // -> 10 kHz
        g_enc_tim.SetCallback(EncTick, nullptr);
        g_enc_tim.Start();
        hw.PrintLine("  encoder sampler: TIM4 @ 10 kHz (base %lu Hz)", (unsigned long)base);
    }
    else
    {
        hw.PrintLine("  ⚠ encoder sampler FAILED to init -- detents will be missed");
    }
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

        backing_scan();
        hw.PrintLine("  %d backing track(s)", backing_count());
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
                    g_oled_dirty = true;
                    hw.PrintLine("  >> DSP %s%s",
                                 g_dsp_bypass ? "BYPASSED" : "ENGAGED",
                                 (!g_dsp_bypass && g_ir_active) ? " (with IR)" : "");
                }
                if(i == 1 && !now)          // tuner footswitch -- its actual job now
                {
                    g_tuner_on   = !g_tuner_on;
                    g_oled_dirty = true;
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
            hw.PrintLine("        us: flush=%lu render=%lu svc=%lu loop=%lu per=%lums w=%lu",
                         (unsigned long)g_t_flush, (unsigned long)g_t_render,
                         (unsigned long)g_t_service, (unsigned long)g_t_loop,
                         (unsigned long)g_t_period, (unsigned long)g_t_wraps);
            hw.PrintLine("        enc: tick=%lu trans=%lu cnt=%ld raw=%d%d rest=%d%d sub=%d det=%d seen=%c%c%c%c",
                         (unsigned long)g_enc_ticks, (unsigned long)g_enc_trans,
                         (long)g_enc_total,
                         (g_enc_raw >> 1) & 1, g_enc_raw & 1,
                         (g_q_rest >> 1) & 1, g_q_rest & 1,
                         (int)g_q_sub, (int)g_q_per_detent,
                         (g_enc_seen & 1) ? '0' : '.',      // 00
                         (g_enc_seen & 2) ? '1' : '.',      // 01
                         (g_enc_seen & 4) ? '2' : '.',      // 10
                         (g_enc_seen & 8) ? '3' : '.');     // 11
            g_enc_ticks = 0;
            g_enc_trans = 0;
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
        // ⚠ Refill the backing ring from the foreground, never the audio callback: it reads the SD
        // card, which blocks. SERVICE_BUDGET_US caps how long it may spend per pass.
        if(g_cmd_ready)
        {
            HandleCommand(g_cmd);
            g_cmd_len   = 0;
            g_cmd_ready = false;
        }

        const uint32_t loop_t0 = System::GetUs();
        {
            // ⚠ PERIOD IS MEASURED IN MILLISECONDS via GetNow(), deliberately.
            //
            // System::GetUs() is GetTick()/(GetFreq()/1e6) -- a 32-bit hardware counter divided
            // down -- so it WRAPS EVERY ~21.5 SECONDS, and because it is divided it does not wrap
            // cleanly at 2^32. Ordinary unsigned delta arithmetic is therefore WRONG across a wrap,
            // which is what produced per=4273494460 (a 21.5 s backwards jump) on 2026-09-06.
            //
            // GetNow() is HAL_GetTick(): a clean millisecond counter that wraps at 49.7 days. For
            // hunting a multi-second stall, millisecond resolution is ample and correctness matters
            // more than granularity.
            static uint32_t prev_ms = 0;
            uint32_t        now_ms  = System::GetNow();
            if(prev_ms)
            {
                uint32_t d = now_ms - prev_ms;
                if(d > g_t_period)
                    g_t_period = d;
            }
            prev_ms = now_ms;
        }

        // Commit a deferred preset change once the encoder has settled.
        if(g_preset_pending >= 0 && (t - g_preset_moved_at) >= kPresetSettleMs)
        {
            int want = g_preset_pending;
            g_preset_pending = -1;
            if(want != g_preset_idx)
                SelectPreset(want);
            g_oled_dirty = true;
        }

        {
            uint32_t t0 = System::GetUs();
            backing_service();
            uint32_t d = System::GetUs() - t0;
            if(d > 1000000u)        // ⚠ straddled a GetUs wrap -- discard, do not report a fiction
                g_t_wraps++;
            else if(d > g_t_service)
                g_t_service = d;
        }

        // Drain the tuner buffer here, where a few hundred microseconds costs nothing.
        if(g_tuner_on)
        {
            // Feed everything written since last time, in order and without gaps.
            int w = g_tun_w;
            while(g_tun_r != w)
            {
                int end = (g_tun_r < w) ? w : kTunRing;
                int n   = end - g_tun_r;
                if(tuner_feed(&g_tun_buf[g_tun_r], n))
                    g_tune = tuner_result();
                g_tun_r = (g_tun_r + n) & (kTunRing - 1);
            }
        }
        else
        {
            g_tun_r = g_tun_w;      // stay in sync while disarmed
        }


        // ⚠ Rotation selects presets. This is what the encoder is FOR, and it frees the tuner
        // footswitch to do its actual job -- cycling presets on a footswitch was always a stopgap.
        // ⚠ BRACES MATTER HERE. A dangling `g_oled_dirty = true;` outside the else-if made the
        // display flush on EVERY loop pass, which was worse than the timer it replaced.
        if(g_enc_watch)
        {
            static uint8_t  last_raw = 0xff;
            static uint32_t last_at  = 0;
            const uint8_t   raw      = g_enc_raw;
            const uint32_t  now_ms   = System::GetNow();
            if(raw != last_raw && (uint32_t)(now_ms - last_at) >= 30)
            {
                last_raw = raw;
                last_at  = now_ms;
                hw.PrintLine("  A=%d B=%d   (D15=A, D16=B; 1 = released/open)",
                             (raw >> 1) & 1, raw & 1);
            }
        }

        int32_t inc = g_enc_delta;
        g_enc_delta = 0;
        if(inc != 0 && g_enc_dbg)
            hw.PrintLine("  enc %+ld  (total %ld)", (long)inc, (long)g_enc_total);

        if(inc != 0)
        {
            // ⚠ APPLY EVERY ACCUMULATED DETENT, not just one. The callback banks each detent into
            // g_enc_delta at 750 Hz, but the foreground drains it far less often -- a flush alone is
            // 24 ms. Collapsing a batch of 3 into a single step threw two thirds of the movement
            // away, which felt like the menu ignoring the knob; forward-then-reverse was worst
            // because opposite detents cancelled inside one batch before anything was applied.
            if(g_in_menu)
            {
                int dir   = inc > 0 ? 1 : -1;
                int steps = inc > 0 ? inc : -inc;
                if(steps > 16)
                    steps = 16;              // a spin should not queue unbounded work
                for(int k = 0; k < steps; k++)
                    menu_event(dir, false);
            }
            else if(g_preset_count > 0)
            {
                int nx = (g_preset_pending >= 0 ? g_preset_pending : g_preset_idx) + (int)inc;
                while(nx < 0)                nx += g_preset_count;
                while(nx >= g_preset_count)  nx -= g_preset_count;
                g_preset_pending  = nx;
                g_preset_moved_at = t;
            }
            g_oled_dirty = true;
        }

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
                    oled_clear();
                    OledLine(2, "  ENTERING DFU");
                    OledLine(4, "  flash now:");
                    OledLine(5, "  make program-dfu");
                    oled_flush();
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
                oled_clear();
                OledLine(3, b);
                OledLine(5, "  release to cancel");
                oled_flush();
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
                if(!g_in_menu)
                {
                    menu_open();
                    g_oled_dirty = true;
                    g_in_menu = true;
                }
                else
                {
                    menu_event(0, true);
                    g_oled_dirty = true;
                    if(menu_take_home())
                        g_in_menu = false;
                }
            }
            sw_down_at = 0;
        }

        // Refresh the screen twice a second -- fast enough to feel live when pressing a switch,
        // slow enough that the I2C traffic is not sitting on top of a high-Z audio input.
        static uint32_t last_oled = 0;
        // ⚠ REDRAW ON CHANGE, not on a timer. Even with batched page writes a full frame is real
        // I2C traffic, and redrawing 10x a second regardless of whether anything moved is what made
        // the menu sluggish and starved backing_service(). The menu now repaints the instant the
        // encoder moves -- which feels FASTER than the old 100 ms timer while doing far less work.
        // The home screen still ticks slowly because its meters genuinely change on their own.
        // ⚠ Event-driven and lightly rate-limited. A FULL flush is ~24 ms of I2C (measured), which
        // is why this was 50 ms. Now that oled_flush() sends only the pages whose content changed,
        // a menu move costs ~6 ms and the floor can come down to 12 ms -- the screen keeps up with
        // the knob instead of trailing it.
        if(g_in_menu)
        {
            if(g_oled_dirty && (t - last_oled) >= 12)   // ⚠ was 50 -- affordable now the flush is partial
            {
                g_oled_dirty = false;
                last_oled    = t;
                uint32_t t0 = System::GetUs();
                menu_render();
                uint32_t t1 = System::GetUs();
                oled_flush();
                uint32_t t2 = System::GetUs();
                if(t1 - t0 > 1000000u || t2 - t1 > 1000000u)
                    g_t_wraps++;    // ⚠ GetUs wrap -- see the note on g_t_period
                else
                {
                    if(t1 - t0 > g_t_render) g_t_render = t1 - t0;
                    if(t2 - t1 > g_t_flush)  g_t_flush  = t2 - t1;
                }
            }
        }
        else if(g_oled_dirty || t - last_oled >= 500)
        {
            g_oled_dirty = false;
            last_oled    = t;
            OledHome();
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
        {
            uint32_t d = System::GetUs() - loop_t0;
            if(d > 1000000u)
                g_t_wraps++;
            else if(d > g_t_loop)
                g_t_loop = d;
        }
        System::Delay(1);
    }
}
