# Tone Trixter — Daisy Seed3 variant

Firmware for the Daisy Seed3 (STM32H750) build of the Tone Trixter. This is a **platform variant
of the RP2350 build, not a replacement** — the two run in parallel, and the analogue front end is
shared between them.

## Status

**Bring-up diagnostics build.** `main.cpp` + `board.h` compile and link for the Seed3
(81 KB of 128 KB flash). Not yet flashed to hardware — the board is mid-assembly.

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
- **Watch:** `screen /dev/tty.usbmodem* 115200`. The diagnostics call `StartLog(true)`, which
  **waits for a terminal**, so the boot report is never missed — but the board will appear to hang
  until something opens the port.
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

⚠ The encoder is quadrature: one detent moves **both** A and B. Seeing only one is the signature of
a single bad joint, which is why edges are reported raw rather than decoded at this stage.

### ⚠ Never busy-wait in an idle loop — it cooks the chip

**`System::Delay()` is `HAL_Delay()`, which BUSY-WAITS.** It burns exactly as much power as a tight
loop and reduces nothing. Use **`__WFI()`** to sleep the core until the next interrupt; libDaisy
defines `SysTick_Handler()` at 1 kHz (`sys/system.cpp`), so there is always a wake source at most
1 ms away. ⚠ Without a periodic interrupt `__WFI()` would hang.

**Measured on the 2026-09-05 bring-up**, Seed3 standalone, doing nothing but polling five GPIOs:

| State | STM32H750 temperature |
|---|---|
| `System::Delay(1)` in the loop | **painful to touch** on sustained contact |
| `__WFI()` in the loop | **slightly warm** |
| Bootloader (DFU) | cool |

The bootloader comparison is what proved it was the application rather than the board — the same
single-variable discipline the bench sessions use.

⚠ `__WFI()` idles the **core only**. PLLs, peripherals and the 64 MB SDRAM that `DaisySeed::Init()`
brings up unconditionally keep running, which is why "slightly warm" rather than cool. Note also
that `Init()` defaults to **400 MHz**, not boost's 480 — there is no easy clock win to be had.

⚠ Worth remembering when the DSP chain lands: the core will then be genuinely busy, so the thermal
headroom this bought is headroom that gets spent.

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
