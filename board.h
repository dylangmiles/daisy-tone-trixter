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
// Header col 6, rows 11-17. ⚠ These deliberately do NOT sit on the SPI1 peripheral: the breakout
// presents a fixed pin order that no hardware SPI instance matches, so it must be bit-banged.
// Rows 13 and 15 are cut and jumpered to power, hence the gaps in the D-numbers.
//
// ⚠ Breakout silkscreen is CARD-referenced: its "DO" is data OUT of the card = MISO at the host,
// and "DI" is data IN = MOSI. Do not wire them by name alone.
constexpr uint8_t kSdCs   = 1; // D1 (row 11)
constexpr uint8_t kSdMosi = 2; // D2 (row 12) — breakout "DI"
constexpr uint8_t kSdSck  = 4; // D4 (row 14)
constexpr uint8_t kSdMiso = 6; // D6 (row 16) — breakout "DO"
constexpr uint8_t kSdCd   = 7; // D7 (row 17) — card detect, optional

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
