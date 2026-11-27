// Tone Trixter — Daisy Seed3 board pin map.
//
// SINGLE SOURCE OF TRUTH for firmware pin numbers. These are Daisy "D" numbers as printed on the
// Seed3 pinout, and they are derived from the board layout generator in the private repo
// (private/docs/gen_daisy_board_svg.py) — NOT from the hand-maintained tables in the pinmap doc,
// which have drifted twice. If a pin here disagrees with a document, the generator wins.
//
// Board: 18 cols x 45 rows, Adafruit 1606 cut down. Daisy under-mounted on rows 10-29.
// Row-to-pin relation used below: side A row = 9 + pin, side B row = 50 - pin.

#pragma once

#include "daisy_seed.h"

namespace tt
{
// ---------------------------------------------------------------- OLED (SH1106, 1.3", I2C)
// Board rows 19-25 on col 3. Rows 19/20/24/25 are CUT from the Daisy strip and jumpered; rows
// 21-23 ride their traces straight to the Daisy pins below.
constexpr uint8_t kOledScl = 11; // D11 · I2C1 SCL   (row 21, rides the trace)
constexpr uint8_t kOledSda = 12; // D12 · I2C1 SDA   (row 22, rides the trace)
constexpr uint8_t kOledRes = 13; // D13             (row 23, rides the trace)

// I2C address is STRAPPED, not selected. The module's DC pin is cut free at col 3 row 24 and tied
// to the digital-ground spine, because encoder SW1 owns D14 on that same row.
//
// ⚠ DO NOT drive a DC pin from firmware. There is no GPIO on it — it is hard-tied to ground.
// To move the display to 0x3D later, re-target that one jumper to the +3V3D rail instead.
constexpr uint8_t kOledI2cAddr = 0x3C;

// ---------------------------------------------------------------- Rotary encoder (PEC11R)
// Body rows 30-35, turned 90 degrees so the three in-line terminals land on separate strips.
// A/C/B on col 12 rows 31/32/33; SW1/SW2 on col 6 rows 31/33. C and SW2 go to the DGND rail,
// so A, B and SW1 all read LOW when active and need pull-ups.
constexpr uint8_t kEncA  = 15; // A0 / D15  (row 28)
constexpr uint8_t kEncB  = 16; // A1 / D16  (row 27)
constexpr uint8_t kEncSw = 14; // D14       (row 24) — ⚠ see kOledI2cAddr above

// ---------------------------------------------------------------- Footswitches
// NOT on the perfboard. Panel-mounted in the enclosure face toward the player and wired back by
// flying leads: signal into col 14, ground into the DGND rail at col 17, same row. No cut needed.
constexpr uint8_t kFswBypass = 25; // A10 / D25 (row 18)
constexpr uint8_t kFswTuner  = 21; // A6  / D21 (row 22)

// ---------------------------------------------------------------- microSD (bit-banged SPI)
// ⚠⚠ MUST MATCH sd_spi.h's SD_*_PIN defines — these feed TestSd (the boot report + `sdcheck`),
// a SECOND bit-bang separate from sd_spi.c's real FatFs driver. They were left on the OLD PERFBOARD
// pins (CS=1/MOSI=2/SCK=4/MISO=6) long after sd_spi.h moved to the Rev A PCB (DM3AT) pinout, so the
// diagnostic drove D4/D2/D6 — which on the PCB are DAT0/DAT2/CLK — and read 0xFF forever while the
// real driver worked. Fixed 2026-11-20. ⚠ Bit-banged (no SPI1): the layout matches no SPI instance.
constexpr uint8_t kSdCs   = 1; // D1  PCB DAT3 (CS)
constexpr uint8_t kSdMosi = 5; // D5  PCB CMD  (MOSI)   (was D2 — perfboard)
constexpr uint8_t kSdSck  = 6; // D6  PCB CLK  (SCK)    (was D4 — perfboard)
constexpr uint8_t kSdMiso = 4; // D4  PCB DAT0 (MISO)   (was D6 — perfboard)
constexpr uint8_t kSdCd   = 7; // D7  PCB card detect

// ---------------------------------------------------------------- microSD VDD power switch (Rev A bodge)
// Rev A COLD-BOOT FIX (= Rev B #6, firmware-switched SD VDD, done on Rev A). A logic-level P-channel
// MOSFET high-side switch sits in the SD VDD line: SOURCE = +3V3D, DRAIN = DM3AT pad 4 (the +3V3D
// track to pad 4 is CUT, so the card's VDD is fed only through the FET). D28 drives the gate via R_g
// 1k, with R_gs 100k gate->source. ⚠ ACTIVE LOW: GPIO LOW pulls the gate below source => FET ON =>
// card POWERED; GPIO HIGH or FLOATING (reset) => gate = source => FET OFF => card UNPOWERED. So the
// card is held off through the cold-boot inrush (the POR-latch window) and firmware powers it up
// cleanly once the rails settle. Also enables a full VDD power-cycle recovery for a hung card at
// runtime. See private/docs/sd_coldboot_bodge_revA.md.
constexpr uint8_t kSdPwrEn = 28; // D28 (connector pin 35, NC on the PCB) — SD VDD enable, ACTIVE LOW (LOW = on)

// ---------------------------------------------------------------- CH B / XLR 48 V phantom
// PH_EN net: D26/PD11 → 1k series → 2N7002 gate → pulls the AO3401 P-FET gate low → LM2577 boost
// input powered → 48 V phantom ON. Drive HIGH = phantom ON; LOW/float = OFF (a 100k gate pulldown
// holds it OFF at boot/float, so phantom is OFF on USB-5 V and before firmware drives the pin).
// ⚠ Default LOW. NEVER enable phantom with a dynamic/ribbon mic connected.
constexpr uint8_t kPhantomEn = 26; // D26 / PD11  PCB PH_EN (phantom 48 V enable)

// ---------------------------------------------------------------- Audio
// Handled by libDaisy's SAI setup, listed here for reference only.
// AUDIO IN 1 = pin 16 (row 25) · AUDIO OUT 1 = pin 18 (row 27).
// The front end is the op-amp daughter rev D on the 9 V rail; the Seed3 input is AC-coupled and
// accepts +/-1.8 V, so the daughter drives it through a link (R1) with a 100k DC return (R2).

inline daisy::Pin P(uint8_t d)
{
    return daisy::DaisySeed::GetPin(d);
}

} // namespace tt
