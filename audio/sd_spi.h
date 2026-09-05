// audio/sd_spi.h — SD card over BIT-BANGED SPI. DAISY SEED3 PIN NUMBERS.
//
// ⚠ Bit-banged for a HARDWARE reason, not preference: the breakout presents a fixed pin order
// that matches no SPI instance, and 1-bit SDMMC is impossible because the Daisy's SDMMC CMD line
// is D5 — which this board wires to the microSD module's GND (row 15, cut and jumpered).
//
// Pins are Daisy "D" numbers, header at col 6 rows 11-17:
//   CS=D1 (r11) · MOSI=D2 (r12) · VCC=D3 (r13, cut+jumper) · SCK=D4 (r14)
//   GND=D5 (r15, cut+jumper) · MISO=D6 (r16) · CD=D7 (r17)
//
// ⚠ The breakout silkscreen is CARD-referenced: its "DO" is MISO at the host and "DI" is MOSI.
//
// Read-only: enough to init a card and read 512-byte sectors, which is all FatFs needs to
// load IR / preset files. Init runs at ~250 kHz (SD spec caps identify mode at 400 kHz);
// data transfer then runs at the bit-bang's native speed. Not audio-safe — reads block the
// caller, so only load from SD at deliberate, glitch-tolerant moments (preset/IR switch).
#ifndef TT_SD_SPI_H
#define TT_SD_SPI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SD_MISO_PIN   6   // D6, breakout "DO"
#define SD_CS_PIN     1   // D1
#define SD_SCK_PIN    4   // D4
#define SD_MOSI_PIN   2   // D2, breakout "DI"

bool     sd_init(void);                          // power-up + SPI-mode init; false if no card
bool     sd_read_block(uint32_t lba, uint8_t *dst);   // read one 512-byte sector (LBA)
uint32_t sd_sector_count(void);                  // total 512-byte sectors (from CSD), 0 if unknown
bool     sd_is_ready(void);                      // initialised OK
bool     sd_is_sdhc(void);                       // block-addressed (SDHC/SDXC) vs byte (SDSC)
void     sd_set_verbose(bool v);                 // print a CMD0/CMD8/ACMD41 init trace
void     sd_pin_check(void);                      // DISCONNECT module first: float/short test on GP6/8/9/10

#ifdef __cplusplus
}
#endif

#endif // TT_SD_SPI_H
