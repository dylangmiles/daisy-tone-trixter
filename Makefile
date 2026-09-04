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

CPP_SOURCES = main.cpp

# SDKs live outside the repo, alongside the Pico SDK, so they are never committed here.
# Override on the command line if yours is elsewhere:  make LIBDAISY_DIR=/path/to/libDaisy
LIBDAISY_DIR ?= /Users/dylan/dev/sdk/daisy/libDaisy
DAISYSP_DIR  ?= /Users/dylan/dev/sdk/daisy/DaisySP

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
