# Tone Trixter — Daisy Seed3

A guitar pedal that makes a piezo pickup sound like a studio microphone — in real time.

**Build log & demos:** [dylangmiles.github.io/tone-trixter](https://dylangmiles.github.io/tone-trixter/)

Piezo pickups are cheap and reliable but have a characteristic harsh, nasal "quack" that EQ alone
can't fix. Tone Trixter applies an **acoustic body impulse response** to the live piezo signal using
FFT convolution, transforming it to match a condenser microphone placed in front of the same guitar.

**Status: the active development path.** The pedal plays — guitar in, DSP chain, SD-loaded IR,
backing tracks, tuner and OLED menu all running on hardware from a 9 V supply. New work happens
here; the [RP2350 build](https://github.com/dylangmiles/pico-tone-trixter) is the V1 prototype that
proved the idea and is still gigging.

---

## Why the Seed3

The V1 measurements kept arriving at the same conclusion: **the analogue front end sets the noise
floor**, not the DSP. The RP2350 build's ES8388 codec was itself a meaningful part of the noise
budget, which made that conclusion hard to test cleanly.

The Seed3's TAC5242 is 32-bit / 192 kHz with a −120 dB floor. The codec effectively stops being a
factor, and the front-end buffer becomes the *only* thing that matters for noise — which is exactly
the question this project turns on. The 480 MHz Cortex-M7 is a bonus rather than the reason.

---

## Hardware

**Processor:** Electrosmith Daisy Seed3 — STM32H750, Cortex-M7 @ 480 MHz, hardware FPU, 64 MB SDRAM
**Codec:** TI TAC5242 — 32-bit / 192 kHz, −120 dB noise floor, hardware-strapped

```
Guitar piezo → buffer daughter board (JFET follower, or OPA1642 op-amp)
  → Seed3 ADC
    → 2048-tap IR convolution (inline) → input trim → 3-band EQ
      → compressor/limiter → output level
    → backing-track mix (after the chain, deliberately)
  → Seed3 DAC → PA / headphones / interface
```

The piezo buffer lives on a swappable 5-pin daughter board, shared with the V1 build. ⚠ **The Seed3
has no input buffer of its own** and its input is line-level, so the high-impedance buffer stage is
mandatory rather than optional.

Built on cut-down perfboard with the Seed under-mounted, 9 V jack power, SH1106 OLED + rotary
encoder, two footswitches (tuner / DSP bypass), and a bit-banged microSD slot.

---

## Features

- **2048-tap IR convolution**, running inline on the M7 — no second core needed
- **DSP chain:** input trim → 3-band EQ (RBJ biquads) → compressor/limiter → output level
- **Presets** bundling all parameters plus IR selection; switching guitars re-stages the whole chain
- **SD card:** drop `tonetrix/` on a card to override presets (`presets.txt`) and add IRs
  (`ir/*.wav`) without reflashing
- **Backing tracks** streamed from the card, mixed *after* the chain so the bed never goes through
  the guitar's IR, EQ or compressor
- **YIN tuner**, full-screen, footswitch-toggled, muting, fed from the raw input
- **OLED home screen:** preset, IR, CPU load, and in / gain-reduction / out meters with a sticky
  clip indicator
- **Live USB-serial control:** every parameter adjustable while playing (`help` lists commands),
  plus timing, CPU and encoder diagnostics

⚠ **Boot is complete passthrough by design.** The pedal always starts transparent, so anything
measured at boot reflects the front end alone with no DSP on top — which matters while the analogue
path is still being characterised.

---

## Measured performance

Steady state, 48 kHz, 64-sample blocks, chain + 2048-tap IR engaged:

| Figure | Reading |
|---|---|
| Audio CPU, bypass | 1 % |
| Audio CPU, chain + IR | **16 % avg, 45 % peak** |
| Foreground loop | 6.6 ms worst, 8 ms period |
| Backing ring | 98–99 %, 0 underruns |
| Block latency | 1.33 ms |

⚠ **Two budgets, and only one is tight.** Audio has ~84 % spare — the foreground is the constrained
side, where a full OLED flush costs ~28 ms. Any optimisation belongs there, not in the audio path.

---

## Build and flash

Requires the ARM GCC toolchain, `dfu-util`, and [libDaisy](https://github.com/electro-smith/libDaisy)
+ [DaisySP](https://github.com/electro-smith/DaisySP).

```sh
make                                          # or: make LIBDAISY_DIR=/path/to/libDaisy
make program-boot                             # ONCE — installs the Daisy bootloader
make program-dfu                              # thereafter
screen /dev/tty.usbmodem* 115200              # console
```

⚠ **libDaisy must be the `seed3-updates` branch, pinned to a commit.** Seed3 support is not on
`main` — a stock libDaisy configures the wrong codec, and an unmerged branch can be force-pushed
underneath you.

⚠ **`APP_TYPE = BOOT_SRAM`.** The STM32H750 has only 128 KB of internal flash and this app does not
fit; it runs from 480 KB of internal SRAM via the Daisy bootloader, which is why `make program-boot`
is a required one-time step.

---

## Notes worth knowing before you read the code

Several things here deliberately do *not* use the obvious library call. Each has a measured reason,
documented at the point of use:

- **Our own SH1106 driver.** libDaisy's OLED transport sends one I²C transaction *per byte* — about
  77 ms per frame. Ours writes a page per transaction and sends only pages that changed.
- **Our own quadrature decode.** libDaisy's `Encoder::Increment()` needs one exact sample pattern
  that a detented PEC11R rarely produces; it works only within a narrow band of turn speeds.
- **Bit-banged SD over SPI.** The Daisy's SDMMC `CMD` pin is wired to ground on this layout, and the
  breakout's pin order matches no hardware SPI instance.
- **`-u _printf_float` in the Makefile.** libDaisy links `--specs=nano.specs`, and newlib-nano omits
  floating-point conversion from `printf` — every `%f` silently prints nothing without it.

`CLAUDE.md` carries the full engineering detail, including the hardware gotchas that cost bench time.

---

## Relationship to the RP2350 build

The two pedals share the analogue front end, the DSP chain design, the IR capture method, and **one
physical SD card** — the card layout lives once, in the
[Pico repo](https://github.com/dylangmiles/pico-tone-trixter), and both firmwares read it.

Driver sources that could reasonably be shared are kept byte-identical between the builds behind
small platform shims, so a protocol or layout fix on either platform applies to both.
