// audio/sd_spi.h — SD card over BIT-BANGED SPI. DAISY SEED3 "D" PIN NUMBERS.
//
// ⚠ Bit-banged, not SDMMC: the original perfboard's breakout presented a fixed pin order that
// matched no SPI instance, and 1-bit SDMMC needed the CMD line on a pin the layout couldn't give.
// The manufactured PCB keeps bit-bang so the hard-won sd_spi.c protocol driver stays shared.
//
// ⚠⚠ PIN WIRING DIFFERS BY BOARD — set the #defines below to match the board being flashed:
//
//   Rev A PCB  (CURRENT — DM3AT socket, JLC-assembled):
//     CS=D1 · MOSI=D5 · SCK=D6 · MISO=D4
//     socket→net→Daisy:  DAT3→CS(D1) · CMD→MOSI(D5) · CLK→SCK(D6) · DAT0→MISO(D4)
//     (confirmed vs the PCB schematic note 2026-11-20; proto pins gave "no card" on this board)
//
//   ORIGINAL PERFBOARD  (Adafruit breakout — SUPERSEDED; restore these defines to reflash it):
//     CS=D1 (r11) · MOSI=D2 (r12) · VCC=D3 (r13, cut+jumper) · SCK=D4 (r14)
//     GND=D5 (r15, cut+jumper) · MISO=D6 (r16) · CD=D7 (r17)
//     ⚠ There D5 (SDMMC CMD) was cut+jumpered to the module's GND (row 15).
//     ⚠ Breakout silk is CARD-referenced: its "DO" is MISO, its "DI" is MOSI.
//
// Reads for FatFs (IR / preset files); writes exist for USB DRIVE MODE (main.cpp), where the host
// owns the card through the MSC class -- FatFs itself stays read-only (sd_diskio.c). Init runs at ~250 kHz (SD spec caps identify mode at 400 kHz);
// data transfer then runs at the bit-bang's native speed. Not audio-safe — reads block the
// caller, so only load from SD at deliberate, glitch-tolerant moments (preset/IR switch).
#ifndef TT_SD_SPI_H
#define TT_SD_SPI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Rev A PCB (see header). ⚠ Perfboard values in ( ) — restore them to reflash the perfboard.
#define SD_MISO_PIN   4   // D4  PCB DAT0   (perfboard: D6, breakout "DO")
#define SD_CS_PIN     1   // D1  PCB DAT3   (perfboard: D1, unchanged)
#define SD_SCK_PIN    6   // D6  PCB CLK    (perfboard: D4)
#define SD_MOSI_PIN   5   // D5  PCB CMD    (perfboard: D2, breakout "DI")

bool     sd_init(void);                          // power-up + SPI-mode init; false if no card
bool     sd_read_block(uint32_t lba, uint8_t *dst);   // read one 512-byte sector (LBA)
bool     sd_write_blocks(uint32_t lba, const uint8_t *src, uint32_t n);  // CMD25, n sectors
uint32_t sd_sector_count(void);                  // total 512-byte sectors (from CSD), 0 if unknown
bool     sd_is_ready(void);                      // initialised OK
bool     sd_is_sdhc(void);                       // block-addressed (SDHC/SDXC) vs byte (SDSC)
void     sd_set_offline(void);                   // mark not-ready (card pulled); next sd_init() re-inits
void     sd_set_verbose(bool v);                 // print a CMD0/CMD8/ACMD41 init trace
void     sd_pin_check(void);                      // DISCONNECT module first: float/short test on GP6/8/9/10

#ifdef TT_SD_SDMMC
// Native SDMMC1 driver only (audio/sd_sdmmc.c, `make SD_SDMMC=1`). It has the same API as above,
// plus a true multi-block read (CMD18), which is where the speed comes from. sd_spi.c stays
// byte-identical to the Pico build, so callers use this under the same #ifdef and fall back to
// sd_read_block() loops otherwise.
bool     sd_read_blocks(uint32_t lba, uint8_t *dst, uint32_t n);
uint32_t sd_last_error(void);                    // last HAL_SD ErrorCode (0 = none)
#endif

#ifdef __cplusplus
}
#endif

#endif // TT_SD_SPI_H
