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

## ⚠ APP_TYPE = BOOT_SRAM — the app does NOT run from internal flash

The STM32H750 has only **128 KB of internal flash**, and the ported DSP chain overflows it: the
diagnostics alone were ~102 KB, and `dsp_chain` + the FFTConvolver add ~40 KB more.

`APP_TYPE = BOOT_SRAM` runs the app from **480 KB of internal SRAM** instead — currently **~24 %
used**. Chosen over `BOOT_QSPI` because code runs from fast internal SRAM rather than external
flash, which matters for a convolver.

⚠ **ONE-TIME SETUP, required before the first flash:**

```
make program-boot      # installs the Daisy bootloader (needs the board in DFU)
```

After that `make flash` works as before. ⚠ Startup is slightly slower — the bootloader copies the
app into SRAM first. And hold-to-DFU now uses `BootloaderMode::DAISY`, not `STM`: jumping to the
STM ROM bootloader would bypass the Daisy bootloader the app depends on.

## The ported DSP chain

`dsp_chain.cpp`, `biquad.h` and the HiFi-LoFi **FFTConvolver** compiled for the H750 **with no
changes at all** — the RP2350 implementation was written against `stdio/string/stdlib/math` only.

⚠ **Order matches the Pico: IR convolution FIRST, then the chain** (EQ → Dynamics → Output level).

⚠ **Default boot state is COMPLETE PASSTHROUGH** — `dsp_chain_init(48000, 1.0)` then
`g_dsp_bypass = true`. The pedal always starts transparent, so anything measured at boot reflects
the **front end alone** with no DSP on top. That matters while the analogue path is still being
characterised.

⚠ **The IR is NOT embedded.** It lives on the SD card and loads when a preset asks for one, so it
costs nothing until wanted. Boot is IR-less by design, which is why embedding was dropped.

Block size is **64** (1.33 ms at 48 kHz) — FFT convolution needs a real block, not the 4 the
diagnostics used.

⚠ **The Pico's Core-1 tail offload does NOT port.** The H750 is single-core; the convolver runs
inline. It is a 480 MHz M7 with an FPU against a 150 MHz M33, so the budget should be there — but
that is an assumption until measured with an IR actually loaded.

## ⚠ The SD card template is NOT duplicated here

The card layout lives once, in **`tone-trixter/pico/sdcard_template/`** (6.8 MB — `config.txt`,
`presets.txt`, IR WAVs, backing tracks).

⚠ **Both pedals read the SAME physical card.** Copying the template into this repo would mean two
copies that drift, and drift shows up as a preset that works on one pedal and not the other — which
reads as a firmware difference and is miserable to chase. One artifact, one home.

Its location under `pico/` is historical rather than meaningful. If it ever needs handing to a user
independently it should become its own repo; until then a pointer beats a copy.

### ⚠ `bk.level` is a Daisy-only preset key

`presets.txt` may carry `bk.level = <0..2>` per preset (backing-track level). **The Pico's parser
ignores unknown keys**, so the card stays readable by both pedals.

⚠ **This is a DELIBERATE, ACCEPTED fork** (decided 2026-09-06) — `dsp_chain.h` and `tt_store.cpp`
differ from the RP2350 copies here and are **not** to be reconciled. Do not "fix" the divergence.
The shared-source rule that governs `sd_spi.c` and `menu.cpp` does not extend to this key: the cost
of the fork is bounded because the card format stays compatible in both directions, which is the
property that actually matters when one physical card feeds both pedals.

⚠ `bk_level` of **0 means "not specified"**, not "silence". The built-in preset table predates the
field and zero-initialises it, so treating 0 as a real level would mute the backing track on every
preset that has not opted in.

## SD card: presets and IRs

Ported from the RP2350 build: `sd_spi.c` (bit-banged SPI), `sd_diskio.c`, `tt_store.cpp` (preset
parsing) and `wav_load.c`. ⚠ **Everything here is OPTIONAL** — no card, no presets, no IR, and the
pedal still boots and passes audio. A missing SD card must never be the difference between a working
pedal and a dead one.

### ⚠ Why bit-banged, and why not libDaisy's FatFSInterface

**SDMMC is impossible on this board.** The Daisy's SDMMC `CMD` line is **D5**, which the layout wires
to the microSD module's **GND** (row 15, cut and jumpered). The breakout's fixed pin order also
matches no SPI instance. Hence bit-bang, exactly as §3b specified.

That rules out libDaisy's `FatFSInterface`, which only links its own SDMMC driver or USB host. We
use the bundled FatFs directly and register a driver through `FATFS_LinkDriver`.

⚠ **Do NOT define `disk_read`/`disk_write`/`disk_status`/`disk_ioctl` directly** the way the Pico
build does. libDaisy's `diskio.c` already defines those and dispatches to registered drivers —
defining them here is a duplicate-symbol collision. `sd_diskio.c` exposes a `Diskio_drvTypeDef`
instead.

### ⚠ The GPIO shim — why sd_spi.c is not rewritten

`sd_spi.c` is 229 lines of SD protocol: command framing, the CMD0/CMD8/ACMD41 init dance, CSD
parsing, timeouts. That logic is platform-independent and hard-won; only a handful of GPIO calls
underneath it are Pico-specific. `sd_daisy_shim.h` maps those onto libDaisy so **the driver source
stays identical between both builds** and a protocol fix on either platform applies to both.
Rewriting it would fork the file and guarantee they drift.

Pins are Daisy **D** numbers: `CS=D1 · MOSI=D2 · SCK=D4 · MISO=D6 · CD=D7`. ⚠ The breakout
silkscreen is **card-referenced** — its `DO` is MISO at the host, its `DI` is MOSI.

### ⚠ libDaisy gap: FatFs code pages

libDaisy compiles FatFs but **not** its code-page tables, while its `ffconf.h` sets `_USE_LFN 1` and
`_CODE_PAGE 850`. So `ff.c` references `ff_convert` and `ff_wtoupper` and nothing defines them —
the link fails. The Makefile adds `Middlewares/Third_Party/FatFs/src/option/ccsbcs.c` to supply both.

### ⚠ IR sample rate is checked, not assumed

`LoadIr()` **rejects** any IR that is not 48 kHz. On the Pico build a mismatched rate silently ran
the convolver at 2× budget and played a 48 kHz IR an octave high — and it sounded plausible enough
not to be noticed for a while. Refusing beats playing something subtly wrong.

Convolver partitioning matches the Pico: head block **64** (= the audio block size, so the
low-latency path costs exactly one block), tail block **512**, up to **4096** taps.

## Footswitch controls (interim, until there is a menu)

| Control | Does |
|---|---|
| **Encoder — turn** | home: select preset (loads its IR) · menu: navigate |
| **Encoder — short press** | home: open the menu · menu: select. Also prints the full report |
| **Encoder — hold 2 s** | enter DFU |
| **Bypass footswitch** | toggle the whole chain — the A/B this project turns on |
| **Tuner footswitch** | toggle the tuner |

⚠ Preset selection moved from the tuner footswitch to the **encoder**, which is what the encoder is
for, and frees the tuner switch to do its actual job. Cycling presets on a footswitch was a stopgap
that existed only because there was no other way to reach a preset with an IR.

⚠ **The `default` preset names no IR by design**, so at boot there is nothing to load and the OLED
shows `ir-`. Cycling with the tuner switch is the only way to reach a preset that has one. That is
not a fault — it is why preset cycling had to exist before the IR could be heard at all.

⚠ **Preset switching is NOT audio-safe, deliberately.** Reading a WAV off the card blocks for a few
hundred milliseconds and the convolver is re-initialised under a running callback — the Pico records
the same "multi-100 ms dropout". `g_ir_active` is cleared **first** so the callback stops touching
the convolver before it is re-initialised. ⚠ Never call `SelectPreset()` from the audio callback.

⚠ **Installing presets is not the same as reading them.** `tt_store_load()` reads the card;
`dsp_chain_install_presets()` hands them to the chain. Without the second call
`dsp_chain_find_preset()` searches the built-in table and returns −1 for every name on the card —
which silently produced "3 presets, no IR" for a while. If the named boot preset is missing, fall
back to the **first** rather than to none.

## ⚠ We do NOT use libDaisy's OledDisplay

`libDaisy`'s `SSD130xI2CTransport::SendData` sends **one blocking I²C transaction per byte**
(`src/dev/oled_ssd130x.h`): **1024 transactions** for a 128×64 frame, each with address, control byte
and ACKs — roughly **77 ms per full flush** at 400 kHz.

Measured consequence (2026-09-06): with the menu redrawing at 100 ms the foreground loop was **~77 %
blocked**, which starved `g_enc.Debounce()` (encoder missed clicks), starved `backing_service()`
(backing track broke up), and made the menu itself sluggish. ⚠ The home screen redrew at 500 ms and
was fine — that asymmetry is what pointed at the display rather than at any of the four subsystems
that appeared broken.

`audio/oled_shim.cpp` is our own SH1106 driver writing **a whole page per transaction**: 16
transactions instead of 1024. ⚠ It also handles the **SH1106's 2-column offset** — its RAM is 132
columns wide with the visible 128 centred, so a driver written for an SSD1306 shifts the image and
wraps the right edge.

⚠ **Redraw ON CHANGE, not on a timer.** Even batched, a full frame is real bus traffic. The menu now
repaints the instant the encoder moves — which feels *faster* than the old 100 ms timer while doing
far less work. Only the home screen still ticks, because its meters change on their own.

## ⚠ libDaisy gotcha: System::GetUs() wraps every ~21.5 seconds

```c
uint32_t GetUs() { return GetTick() / (GetFreq() / 1000000); }
```

`GetTick()` is a 32-bit hardware counter. Divided down to microseconds it **wraps roughly every 21.5
seconds**, and because it is a *divided* counter it does **not** wrap cleanly at `2^32` — so ordinary
unsigned delta arithmetic is wrong across a wrap. Observed 2026-09-06 as a period reading of
`4273494460` µs, i.e. a 21.5 s jump backwards.

**Rules:**
- ⚠ Use **`System::GetNow()`** (HAL_GetTick, clean milliseconds, wraps at 49.7 days) for anything
  longer than a few seconds, or for any duration that might straddle a wrap.
- `GetUs()` is fine for genuinely short intervals, but **discard implausible results** rather than
  reporting them — the instrumentation counts these as `wraps=`.
- ✅ **`tt_shim_now_us()` no longer uses `GetUs()`** (fixed 2026-09-06). It accumulates raw
  `GetTick()` deltas into a 64-bit counter, so the microseconds it returns roll over cleanly at
  `2^32` and delta arithmetic is correct. ⚠ It is **foreground-only** — the static state is not
  reentrant. `backing.cpp` and `sd_spi.c` are the only callers.
  The bug it fixes was not cosmetic: `backing_service()` bounds its work with
  `(time_us_32() - t0) < SERVICE_BUDGET_US`, and across a `GetUs()` rollover that went true
  immediately, so **once every 21.5 s the backing track was starved for no reason**. It also
  produced `max service=4273495447 us` in `bk stat`.

## Terminal commands

⚠ Restored from the RP2350 build. `dsp_chain_command()` was already ported and implements the whole
stage/param language — it only ever needed a line of text delivered to it. Board-level commands sit
in front of it in `HandleCommand()`.

`help` · `status` · `presets` · `preset <n|name>` · `bypass on|off` · `tuner on|off` · `gr on|off` ·
`bk` · `bk <n>|off` · `bklevel <0..2>` · `dfu` — plus every `dsp_chain` command.

⚠ Commands beat the encoder for anything diagnostic: **the output goes to the terminal anyway**, so
having the request there keeps question and answer together, and a keyboard beats a rotary encoder
for typing a parameter value.

The USB RX callback runs in **interrupt context** and does the minimum — copy bytes into a line
buffer, set a flag. Parsing happens in the foreground.

## The menu

`menu.cpp` and `menu.h` ported **unchanged**. It only used three of the Pico OLED driver's
functions — `oled_clear`, `oled_text`, `oled_text_inv` — so `oled_shim.cpp` provides those on top of
libDaisy's `OledDisplay` and the menu compiles as-is. A layout fix on either platform then applies
to both, rather than the two versions drifting.

⚠ **`oled_text_inv` is a filled bar plus text drawn "off"** — libDaisy has no inverted-text call, and
the selected row depends on it to be readable at a glance. Without it every row looks identical and
the menu is unusable.

⚠ **The Pico's async/DMA flush machinery is deliberately NOT reproduced.** It existed because the
RP2350 convolved in a Core 0 foreground loop that a blocking 180 ms I²C flush would stall
([[project_oled_async_dma_flush]]). Here audio runs in a DMA-driven callback the main loop cannot
stall, so a plain blocking flush is harmless.

⚠ **The menu redraws at 100 ms, the home screen at 500 ms.** Navigation has to feel immediate under
the fingers; glanceable data does not.

### ⚠ Two hooks that are honest stubs

- **`app_pga_*` — there is NO PGA on this board.** The Pico drove the ES8388's input PGA (0..+24 dB
  in 3 dB steps); the Seed3's TAC5242 is hardware-strapped with no software gain control, and the
  front-end level is set by the analogue daughter instead. Reporting 0 dB keeps the menu honest
  rather than showing a control that does nothing.
- ~~`backing_*`~~ ✅ **ported** — see below.

## Backing tracks

`backing.cpp` ported with only **two** platform calls shimmed: `I2S_SAMPLE_RATE` (a constant) and
`time_us_32()`. The streaming engine itself is portable.

⚠ **`backing_mix()` runs AFTER the chain**, exactly as on the Pico — *"drums AFTER the chain: no
IR/EQ/comp on the bed"*. The track is already-produced audio; running it through the guitar's
processing would be both wrong and a waste of the budget.

⚠ **`backing_service()` runs in the FOREGROUND, never the audio callback.** It reads the SD card,
which blocks; `SERVICE_BUDGET_US` caps how long it may spend per pass.

### ⚠ The 64 KB ring lives in SDRAM

`s_ring[32768]` int16 fitted in the RP2350's main RAM but **overflowed DTCMRAM by 976 bytes here and
refused to link**. It is now `DSY_SDRAM_BSS`. The Daisy has 64 MB of SDRAM nothing else touches, and
a streamed audio ring is exactly what it suits — sequential access, refilled in the foreground,
never on a latency-critical path.

✅ **`SERVICE_BUDGET_US` (2000) vs `CHUNK_BYTES` (256) VERIFIED on this board, 2026-09-06.**
`bk stat` with a track playing reports **ring 98–99 %, 0 underruns** in steady state. The Pico's
failure — *the budget must exceed one chunk read or the ring silently sits at 2 %* — **does not occur
here**; the bit-bang read is fast enough that granularity is not the constraint. No change needed.

⚠ `svc=` peaks around **6 ms**, well over the 2000 µs budget, and that is expected rather than a
fault: the budget is checked **before starting** each chunk, so one slow card read overshoots it. It
bounds when to stop issuing reads, not the duration of one. With the ring at 99 % and no underruns
there is nothing to chase.

## Tuner and GR meter

**Tuner** (`tuner.cpp`) ported unchanged — it depends only on `math.h`/`string.h`. ⚠ It is fed from
the **RAW INPUT, before any DSP**: you tune the string, not the compressed and EQ'd version of it.
Gated behind `g_tuner_on` so its autocorrelation costs nothing when unused. When armed it takes the
OLED's input line, because tuning matters more than levels at the moment you are doing it.

**GR meter** shares the existing meter line rather than getting its own repaint. ⚠ On the Pico the
GR meter defaulted **off** because its extra I²C repaints coupled EMI into the high-Z input
([[project_home_gr_meter_crosstalk]]). Folding it into the existing 2 Hz refresh adds no bus traffic,
so here it costs nothing to leave on.

## The bypass footswitch is the A/B control

⚠ Interim, until there is a menu. Boot is passthrough; **pressing the bypass footswitch toggles the
chain** (and the IR with it). That is the comparison this whole project turns on — the same playing,
with and without, switched instantly.

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

### Reporting: OLED, periodic line, on-demand detail

The board reports three ways, because no single one is reliable:

| Route | When it matters |
|---|---|
| **OLED** — 8 lines, refreshed 2 Hz | ⚠ the only readout with **no cable attached** — what you use once the enclosure is closed |
| **Periodic serial line**, every 5 s | live state while a console is attached |
| **On-demand report** — *short press the encoder switch* | the full detail, with live input states |

### Encoder switch = the control surface during bring-up

| Action | Result |
|---|---|
| short press | re-print the full report to serial |
| **hold 2 s** | countdown on the OLED, then **jump to DFU** |

⚠ **Hold-to-DFU may be the only way to flash this board.** The Daisy is **under-mounted**, so its
`BOOT` and `RESET` buttons face the perfboard. `System::ResetToBootloader()` removes the need for
them. Then `make flash` builds and programs in one step.

⚠ **It is a convenience layered on the physical buttons, not a replacement.** Firmware that hangs
before the monitor loop never reaches the hold check. The **6 flashes then 1 Hz** LED signal is how
you know the loop was reached and hold-to-DFU is available.

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
