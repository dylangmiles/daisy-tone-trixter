# Tone Trixter — Daisy Seed3 variant

Firmware for the Daisy Seed3 (STM32H750) build of the Tone Trixter. This is a **platform variant
of the RP2350 build, not a replacement** — the two run in parallel, and the analogue front end is
shared between them.

## Status

**ALL DIGITAL PERIPHERALS VERIFIED ON HARDWARE (2026-09-05)**, on both USB and 9 V-jack power:

| Subsystem | Evidence |
|---|---|
| Board identity | reports **Daisy Seed3** — confirms the pinned `seed3-updates` branch |
| OLED | answers at **`0x3C`** — proves the I²C conversion, the `DC` ground strap (not `0x3D`), and both cut-and-jumper power runs |
| microSD | **CMD0 = `0x01`** with a card, **`0xFF`** without — and ⚠ *not* `0x00`, so MISO is not shorted |
| Encoder | A/B show **correct quadrature** — lead/lag swaps with direction — plus SW |
| Footswitches | both give clean edges, **no crosstalk** between the two ribbon leads |
| GPIO rows | no shorts, established on the bare board before anything was attached |
| Power | jack → D1 → rail → VIN, loaded at **8.86 V** from a 9.12 V supply |

⚠ An elegant cross-check falls out of this: the row 24 cut isolates the OLED's `DC` **and** encoder
`SW1` still works on D14. If that cut had failed, the `DC` strap would ground D14 and `SW` would read
stuck LOW. Both halves of the D14 conflict confirmed by independent readings.

**Remaining: the front-end daughter, and then the analogue path** — where it stops being a wiring
exercise and becomes an audio one.

## Related repos

All under `/Users/dylan/dev/dylangmiles.github.com/`, opened together via the `tone-trixter/`
umbrella directory:

| Path | Repo | Holds |
|---|---|---|
| `tone-trixter/daisy/` | `daisy-tone-trixter` (public) | this — Daisy firmware |
| `tone-trixter/pico/` | `pico-tone-trixter` (public) | RP2350 firmware |
| `tone-trixter/blog/` | `tone-trixter` (public) | the build-log site, both platforms |
| `tone-trixter/private/` | `tone-trixter-private` | bench docs, debug sessions, per-build BOMs |
| `../electronics-inventory-private/` | `electronics-inventory-private` | parts stock, orders, receipts |

Debug material and work-in-progress go in the **private** repo. Only finished, polished stories
get published to the blog.

## Hardware

**Daisy Seed3** — STM32H750, Cortex-M7 @ 480 MHz, 64 MB SDRAM, USB-C. Pin-to-pin compatible with
the earlier Daisy Seed, so external circuitry is unchanged.

Codec is the **TI TAC5242**: 32-bit / 192 kHz, −120 dB noise floor. This matters for the project's
central question — on the RP2350 build the ES8388 was a real part of the noise budget, whereas here
**the analogue front end becomes the determining factor for noise floor.** The JFET buffer work
carries over directly and matters more, not less.

**The front-end buffer is still required.** The Daisy's codec input is line-level and the K&K
pickup is high-Z (≥1 MΩ), so the JFET source-follower daughter sits in front of the ADC exactly as
it does on the Pico build. Current Q1 is the Communica 2N3819 "BB20" at R1=10 M / R2=3.3 M,
V_S ≈ 2.4 V (the 2026-08-08 rebias). Design docs and measurements live in the private repo.

**Debug header:** on the Seed3 the JTAG/SWD header is an *unpopulated through-hole footprint* — a
header must be soldered on before ST-LINK SWD debugging is possible. Flashing over USB DFU needs
no header and no probe.

## Environment

| Tool | Status |
|---|---|
| `arm-none-eabi-gcc` | ✅ 12.3 at `/Applications/ArmGNUToolchain/12.3.rel1/arm-none-eabi/` |
| `dfu-util` | ✅ `/opt/homebrew/bin/dfu-util` — this is the normal flashing path |
| `cmake` | ✅ `/opt/homebrew/bin/cmake` |
| `openocd` | ❌ **not installed** — needed only for ST-LINK SWD step/breakpoint debug |
| libDaisy | ✅ `/Users/dylan/dev/sdk/daisy/libDaisy` — ⚠ **branch `seed3-updates`, pinned to `e1f740a`** |
| DaisySP | ✅ `/Users/dylan/dev/sdk/daisy/DaisySP`, built |

## Build and flash

- **Build:** `make` (libDaisy's Makefile flow). Override the SDK path with
  `make LIBDAISY_DIR=/path/to/libDaisy` if needed.
- **Flash:** `make program-dfu` — hold BOOT, tap RESET to enter DFU, then run it.
- **Watch:** `screen /dev/tty.usbmodem* 115200`. ⚠ The diagnostics call **`StartLog(false)`** — they
  never wait for a terminal. The verdict is **re-printed every 5 s** in the monitor loop, so a console
  attached at any time sees the state.

  ⚠ **Do not go back to `StartLog(true)`.** It blocks until a PC opens the port, so on a 9 V supply
  with no USB the board hangs before the diagnostics run at all — which makes the onboard LED useless
  as a "did it boot" indicator on adapter or battery power. Cost bench time 2026-09-05.

### LED boot signal

- **6 fast flashes** immediately after `Init()` — booted. Deliberately before anything that can block
  or fail, so it works with no console attached.
- **then 1 Hz** — reached the monitor loop, all checks ran.
- ⚠ **Flashes but no 1 Hz** = hung between `Init()` and the loop, which points at the I²C init — the
  only hardware touched in between.

Verified 2026-09-05 on **both** USB and 9 V-jack power.
- **Debug:** ST-LINK over SWD needs the debug header soldered **and** openocd installed.

### ⚠ libDaisy must be the `seed3-updates` branch

**Seed3 support is not on main.** Main's `BoardVersion` enum stops at `DAISY_SEED_2_DFM` and the
tree contains no TAC5242 reference at all, so a stock libDaisy configures the wrong codec.

The branch adds `DAISY_SEED_1_2` and `DAISY_SEED_3` in a small, focused diff (~76 lines over
`daisy_seed.h/.cpp`, `sai.h/.cpp`). **Pinned to `e1f740a` (2026-08-06, "add 192kHz samplerate to
sai")** on a local branch `seed3-pinned`, because an unmerged branch can be rebased or force-pushed
underneath you. Do not track the branch tip.

⚠ Note the pin is slightly *behind* main — it loses main's post-Aug-6 commits, which at the time of
pinning were a README change and a TCA9534 driver. Neither matters here.

**Seed3 is detected at runtime**, not by a compile-time define: `CheckBoardVersion()` reads `PH6`
tied to GND. So the first diagnostic is simply "does the board say Seed3".

⚠ **There is no TAC5242 driver in libDaisy.** The built codec drivers are `ak4556`, `pcm3060` and
`wm8731`; the branch comment says the Seed3 shares Seed Rev4's SAI configuration. The codec is
therefore hardware-strapped, not software-configured. Do not go looking for a codec init call.

## Bring-up diagnostics

`main.cpp` runs a boot report then a live input monitor. Each check answers one question and
reports pass/fail on its own, so a failure points at a specific joint:

1. **Board identity** — must report Daisy Seed3.
2. **I²C1 bus scan** — expects exactly `0x3C`. This proves the OLED's converted links, its address
   strap and both cut-and-jumper power runs **without any display driver**: a blank screen cannot
   distinguish a bad link from a driver bug, but an address answering can. If `0x3D` answers
   instead, the DC strap went to +3V3D rather than ground.
3. **Digital inputs** — encoder A/B/SW and both footswitches, all active-LOW with internal
   pull-ups, reported idle-state first and then as live edges.
4. **microSD** — bit-banged SPI **CMD0**. ⚠ Bit-banged deliberately: the breakout's fixed pin order
   matches no hardware SPI instance (§3b). Exercises CS/SCK/MOSI/MISO in one shot, and the failure
   modes are distinguishable — `0x01` bus good · `0xFF` no card or CS/SCK/MOSI not arriving ·
   ⚠ `0x00` **MISO stuck low**, which on the Pico build was a solder bridge misread for hours as
   "card not responding" ([[project_sd_card_pinout]]). Meter D6 to GND before suspecting the card.

### ⚠ The repeating summary line IS the report

`StartLog(false)` means the board prints its boot report **before USB CDC enumerates on the host**,
so that output is routinely lost or garbled (`===========$$`). Everything a person needs to read must
appear in the **5-second summary line**, which repeats. ⚠ **Adding a check without adding it to that
line makes it invisible in practice** — exactly what happened when the SD test was first added.

### ⚠ The OLED's RESET must be driven

`RES` (D13) is **active low**. Left as an unconfigured input it floats and the SH1106 comes out of
reset — or does not — differently on each power-up. That was masked until 2026-09-05 by
`StartLog(true)` blocking for seconds; switching to `StartLog(false)` exposed it as an intermittent
that **jiggling could not reproduce, because it was never mechanical**. The firmware now asserts
reset, holds 10 ms, releases, waits 50 ms, and leaves the pin **driven high**. ⚠ A longer delay would
also have "fixed" it and would have been the wrong fix — the delay was the disguise, not the cure.

⚠ The encoder is quadrature: one detent moves **both** A and B. Seeing only one is the signature of
a single bad joint, which is why edges are reported raw rather than decoded at this stage.

### ⚠ The idle loop runs hot, and the obvious fix breaks the USB console

**`System::Delay()` is `HAL_Delay()`, which BUSY-WAITS** — it spins at full clock and reduces
nothing. Measured on the 2026-09-05 bring-up, Seed3 standalone, polling five GPIOs and nothing else:

| Idle loop | STM32H750 | USB console |
|---|---|---|
| `System::Delay(1)` | **painful to touch** | ✅ stays connected |
| `__WFI()` | **slightly warm** | ❌ `screen` disconnects at loop entry, every time |
| bootloader (DFU) | cool | n/a |

The bootloader reading proves the heat is the **application, not the board**. Factory-soldered
headers had already made a solder bridge unlikely, so it was never a short.

**Current state: `System::Delay(1)`, heat accepted.** During bring-up a diagnostic that disconnects
is worse than one that runs warm.

⚠ **UNRESOLVED — why `__WFI()` kills the CDC.** libDaisy's USB suspend callback sets `SLEEPDEEP` and
`SLEEPONEXIT` (`src/usbd/usbd_conf.c:314`), which would turn `__WFI()` into STOP mode and gate the
USB clocks. That was the obvious culprit, but **clearing both bits immediately before `__WFI()` did
not fix it** — so the real cause is still unidentified. Do not re-try `__WFI()` assuming SLEEPDEEP
is the answer; it has already been ruled out.

⚠ **REVISIT once the OLED works.** The serial console is only the primary interface while there is
no display. Report diagnostics to the screen instead and `__WFI()` becomes viable again — which
matters, because the DSP chain will spend the thermal headroom this would have bought.

⚠ `Init()` already defaults to **400 MHz**, not boost's 480, so there is no clock win available.
`__WFI()` also idles the **core only** — PLLs, peripherals and the 64 MB SDRAM that
`DaisySeed::Init()` brings up unconditionally keep running regardless.

**Do not copy the RP2350 debug setup.** The Pico build's `openocd.cfg` carries a `gdb-attach` hook
that resumes core1, working around core1 freezing in `__wfe()` when a probe attaches. The H750 is
single-core; that whole problem and its workaround do not apply here.

## What carries over from the RP2350 build

- **Analogue front end** — unchanged, see above.
- **DSP chain** — the biquad EQ / compressor-limiter / 512-tap IR convolution design ports to
  libDaisy + DaisySP (C++). The Pico implementation is in `tone-trixter/pico/audio/`.
- **Bench conventions** — dated debug-session directories with a README per investigation, capture
  filenames that state the setting and the observation, colour-blind-safe plots (distinct dash
  patterns and direct end-labels, never colour alone).

## What to be careful about

- **Sample rate must match end to end.** On the Pico build a 96 kHz double-speed codec setting
  silently ran the convolver at 2× budget and played a 48 kHz IR an octave high. Verify the codec
  rate, the block size and the IR rate agree before trusting any audio result.
- **Builder is colour blind** — meter every resistor value with a DMM, never read colour bands.
