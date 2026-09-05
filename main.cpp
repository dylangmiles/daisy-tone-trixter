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

using namespace daisy;

static DaisySeed hw;

// Inputs that read LOW when active. Encoder C and SW2 go to the DGND rail, and the footswitches
// ground their signal line, so every one of these needs an internal pull-up.
struct Input
{
    const char* name;
    uint8_t     pin;
    GPIO        gpio;
    bool        last;
};

static Input inputs[] = {
    {"encoder A", tt::kEncA, {}, true},
    {"encoder B", tt::kEncB, {}, true},
    {"encoder SW", tt::kEncSw, {}, true},
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

    hw.PrintLine("  card detect (D%d): %s", tt::kSdCd, sd_cd.Read() ? "HIGH" : "LOW");

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
    failures += i2c_ok ? 0 : 1;

    hw.PrintLine("");
    hw.PrintLine("[3] digital inputs -- all active LOW, internal pull-ups");

    for(size_t i = 0; i < kNumInputs; i++)
        inputs[i].gpio.Init(tt::P(inputs[i].pin), GPIO::Mode::INPUT, GPIO::Pull::PULLUP);

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
    failures += sd_ok ? 0 : 1;

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
            hw.PrintLine("[%lus] board=%s · i2c 0x%02X=%s · sd CMD0=0x%02X %s · inputs %s",
                         (unsigned long)(t / 1000),
                         board_ok ? "Seed3 ok" : "WRONG",
                         tt::kOledI2cAddr,
                         i2c_ok ? "FOUND" : "absent",
                         g_sd_r1,
                         sd_ok ? "ok" : (g_sd_r1 == 0x00 ? "MISO-LOW!" : "no-card"),
                         "edges below");
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
