# Tone Trixter — Daisy Seed3 variant

Firmware for the Daisy Seed3 (STM32H750) build of the Tone Trixter. This is a **platform variant
of the RP2350 build, not a replacement** — the two run in parallel, and the analogue front end is
shared between them.

## Status

**Scaffold only.** No firmware written yet, no SDK fetched. See "Environment" for what is and is
not set up on this machine.

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
| libDaisy | ❌ **not fetched** |
| DaisySP | ❌ **not fetched** |

## Build and flash

To be filled in once libDaisy is fetched and a first target builds. Expected shape:

- Build: `make` (libDaisy's Makefile-based flow) or CMake, TBD
- Flash: `make program-dfu` — hold BOOT, tap RESET to enter DFU, then flash over USB-C
- Debug: ST-LINK over SWD, requires soldering the debug header **and** installing openocd

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
