# Tone Trixter — Daisy Seed3
#
# Build:  make
# Flash:  make program-dfu     (hold BOOT, tap RESET to enter DFU, then run this)
# Watch:  screen /dev/tty.usbmodem* 115200
#
# ⚠ libDaisy must be on the seed3-updates branch, pinned to e1f740a. Seed3 support is NOT on main:
# main's board enum stops at DAISY_SEED_2_DFM and has no TAC5242 entry, so a stock libDaisy would
# configure the wrong codec. See CLAUDE.md.

TARGET = tone_trixter_daisy

# ⚠ BOOT_SRAM, not the default BOOT_NONE. The STM32H750 has only 128 KB of INTERNAL flash and the
# port overflows it: dsp_chain + the FFTConvolver alone are ~40 KB on top of ~100 KB of diagnostics.
# BOOT_SRAM gives 512 KB and runs from fast internal SRAM rather than external QSPI, which matters
# for a convolver.
#
# ⚠ ONE-TIME SETUP: the Daisy bootloader must be installed first --  make program-boot
# After that, `make program-dfu` (or `make flash`) works as before, and startup is slightly slower
# because the bootloader copies the app into SRAM.
APP_TYPE = BOOT_SRAM

C_SOURCES = \
	audio/sd_spi.c \
	audio/sd_diskio.c \
	audio/wav_load.c \
	$(LIBDAISY_DIR)/Middlewares/Third_Party/FatFs/src/option/ccsbcs.c

# ⚠ libDaisy compiles FatFs but NOT its code-page tables, while its ffconf.h sets _USE_LFN 1 --
# so ff.c references ff_convert/ff_wtoupper and nothing defines them. ccsbcs.c supplies both for
# the single-byte code pages. Without it the link fails the moment long filenames are enabled.

CPP_SOURCES = \
	main.cpp \
	audio/dsp_chain.cpp \
	audio/sd_daisy_shim.cpp \
	audio/tt_store.cpp \
	audio/tuner.cpp \
	audio/menu.cpp \
	audio/oled_shim.cpp \
	audio/backing_stub.cpp \
	lib/FFTConvolver/FFTConvolver.cpp \
	lib/FFTConvolver/TwoStageFFTConvolver.cpp \
	lib/FFTConvolver/AudioFFT.cpp \
	lib/FFTConvolver/Utilities.cpp

C_INCLUDES += -I. -Ilib/FFTConvolver \
	-I$(LIBDAISY_DIR)/Middlewares/Third_Party/FatFs/src

# SDKs live outside the repo, alongside the Pico SDK, so they are never committed here.
# Override on the command line if yours is elsewhere:  make LIBDAISY_DIR=/path/to/libDaisy
LIBDAISY_DIR ?= /Users/dylan/dev/sdk/daisy/libDaisy
DAISYSP_DIR  ?= /Users/dylan/dev/sdk/daisy/DaisySP

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# Convenience: build then flash in one step. The board must already be in DFU --
# hold the ENCODER SWITCH for 2 s (see main.cpp), or hold BOOT and tap RESET if
# those buttons are still reachable.
.PHONY: flash
flash: all program-dfu

# Wait for a DFU device to appear, then flash. The Daisy bootloader only opens its DFU window for
# ~2 s at power-up, which is hard to hit by hand -- start this, THEN power-cycle the board.
.PHONY: flash-wait
flash-wait: all
	@echo "Waiting for DFU -- power-cycle the Daisy now (Ctrl-C to give up)..."
	@while ! dfu-util -l 2>/dev/null | grep -qi "0483:df11"; do sleep 0.2; done
	@echo "DFU found, flashing."
	@$(MAKE) program-dfu
