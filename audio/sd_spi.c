// audio/sd_spi.c — bit-banged SD-card SPI driver (SPI mode 0). See sd_spi.h.

#include "audio/sd_spi.h"

// ⚠ Platform calls come from the shim, so this file stays byte-identical to the Pico
// build apart from this include. See audio/sd_daisy_shim.h for why.
#include "audio/sd_daisy_shim.h"
#include <stdio.h>

static bool s_ready   = false;
static bool s_sdhc    = false;   // block (SDHC/SDXC) vs byte (SDSC) addressing
static bool s_slow    = true;    // ~250 kHz during identify, then native speed for data
static bool s_verbose = false;   // print an init trace (bench bring-up)

bool sd_is_ready(void) { return s_ready; }
bool sd_is_sdhc(void)  { return s_sdhc; }
void sd_set_verbose(bool v) { s_verbose = v; }

// Half-clock delay: only during identify (spec caps identify clock at 400 kHz). After init
// the loop runs at bit-bang native speed (a few MHz), which any SPI-mode card accepts.
static inline void sd_hc_delay(void) { if (s_slow) busy_wait_us(2); }

// One byte, MSB first, SPI mode 0: MOSI set while SCK low, MISO sampled on the rising edge.
static uint8_t sd_xfer(uint8_t out) {
    uint8_t in = 0;
    for (int i = 7; i >= 0; i--) {
        gpio_put(SD_MOSI_PIN, (out >> i) & 1);
        sd_hc_delay();
        gpio_put(SD_SCK_PIN, 1);
        sd_hc_delay();
        in = (uint8_t)((in << 1) | (gpio_get(SD_MISO_PIN) ? 1 : 0));
        gpio_put(SD_SCK_PIN, 0);
    }
    return in;
}

static inline void sd_cs(bool select) { gpio_put(SD_CS_PIN, select ? 0 : 1); }  // active-low

// SD command CRC7 (poly x^7+x^3+1), returned with the stop bit set.
static uint8_t sd_crc7(const uint8_t *buf, int len) {
    uint8_t crc = 0;
    for (int i = 0; i < len; i++) {
        uint8_t d = buf[i];
        for (int j = 0; j < 8; j++) {
            crc = (uint8_t)(crc << 1);
            if ((d ^ crc) & 0x80) crc ^= 0x09;
            d = (uint8_t)(d << 1);
        }
    }
    return (uint8_t)((crc << 1) | 1);
}

// ⚠ ITERATION-BOUNDED, NOT CLOCK-BOUNDED. The data path runs inside the USB interrupt in drive
// mode (the MSC middleware calls Read/Write from the OTG ISR at priority 0), where SysTick is
// masked and a millisecond clock stops. A clock-based timeout there never expires and a card
// fault becomes a hang. One sd_xfer is ~3.4 us at native speed (291 kB/s measured), so
// kXfersPerMs converts the old millisecond budgets into loop counts that work in any context.
#define kXfersPerMs 300u

// Poll MISO until the card releases the bus (0xFF = not busy). Best-effort.
static bool sd_wait_ready(uint32_t timeout_ms) {
    for (uint32_t i = 0; i < timeout_ms * kXfersPerMs; i++)
        if (sd_xfer(0xFF) == 0xFF) return true;
    return false;
}

// Send a command (6 bytes) and return the R1 response. CRC7 is computed, so CMD0/CMD8 get
// their required 0x95/0x87 automatically. Caller manages CS and reads any R3/R7 trailer.
static uint8_t sd_cmd(uint8_t cmd, uint32_t arg) {
    sd_wait_ready(250);
    uint8_t buf[6] = { (uint8_t)(0x40 | cmd),
                       (uint8_t)(arg >> 24), (uint8_t)(arg >> 16),
                       (uint8_t)(arg >> 8),  (uint8_t)arg, 0 };
    buf[5] = sd_crc7(buf, 5);
    for (int i = 0; i < 6; i++) sd_xfer(buf[i]);
    uint8_t r1 = 0xFF;
    for (int i = 0; i < 16; i++) { r1 = sd_xfer(0xFF); if (!(r1 & 0x80)) break; }
    return r1;
}

// ACMD = CMD55 then the app command.
static uint8_t sd_acmd(uint8_t cmd, uint32_t arg) {
    sd_cmd(55, 0);
    return sd_cmd(cmd, arg);
}

// Float/short test for the SD signal pins. DISCONNECT the module first so we read the
// Pico pin + header wire alone: toggle the internal pull up/down and see if the pin follows.
// floats (pu=1 pd=0) = good; stuck low (pu=0) = short to GND; stuck high (pd=1) = short to VCC.
void sd_pin_check(void) {
    struct { const char *name; int pin; } p[4] = {
        { "MISO GP6 ", SD_MISO_PIN }, { "CS   GP8 ", SD_CS_PIN },
        { "SCK  GP9 ", SD_SCK_PIN },  { "MOSI GP10", SD_MOSI_PIN },
    };
    printf("sd: pin check — DISCONNECT the SD module first:\n");
    for (int i = 0; i < 4; i++) {
        gpio_init(p[i].pin);
        gpio_set_dir(p[i].pin, GPIO_IN);
        gpio_pull_up(p[i].pin);   sleep_ms(2); int hi = gpio_get(p[i].pin);
        gpio_pull_down(p[i].pin); sleep_ms(2); int lo = gpio_get(p[i].pin);
        gpio_disable_pulls(p[i].pin);
        const char *v = (hi && !lo) ? "floats (ok)"
                      : (!hi && !lo) ? "STUCK LOW  → shorted to GND"
                      : (hi && lo)   ? "STUCK HIGH → shorted to VCC"
                                     : "erratic";
        printf("  %s: pu=%d pd=%d → %s\n", p[i].name, hi, lo, v);
    }
}

static void sd_pins_init(void) {
    gpio_init(SD_SCK_PIN);  gpio_set_dir(SD_SCK_PIN, GPIO_OUT);  gpio_put(SD_SCK_PIN, 0);
    gpio_init(SD_MOSI_PIN); gpio_set_dir(SD_MOSI_PIN, GPIO_OUT); gpio_put(SD_MOSI_PIN, 1);
    gpio_init(SD_CS_PIN);   gpio_set_dir(SD_CS_PIN, GPIO_OUT);   gpio_put(SD_CS_PIN, 1);
    gpio_init(SD_MISO_PIN); gpio_set_dir(SD_MISO_PIN, GPIO_IN);  gpio_pull_up(SD_MISO_PIN);
}

bool sd_init(void) {
    s_ready = false;
    s_sdhc  = false;
    s_slow  = true;
    sd_pins_init();

    // >=74 clocks with CS high + MOSI high to enter native SPI mode.
    sd_cs(false);
    for (int i = 0; i < 10; i++) sd_xfer(0xFF);

    sd_cs(true);

    // Raw diagnostic: send one CMD0 and print the bytes the card clocks back. Distinguishes
    // "MISO stuck low" (all 00), "MISO stuck high / no card" (all FF), a genuine 0x01/0x00
    // response, and a bit-shift (e.g. 80 02 …). CS is asserted, MOSI high between bytes.
    if (s_verbose) {
        uint8_t c0[6] = { 0x40, 0, 0, 0, 0, 0x95 };
        for (int i = 0; i < 6; i++) sd_xfer(c0[i]);
        printf("sd:   CMD0 raw resp:");
        for (int i = 0; i < 12; i++) printf(" %02X", sd_xfer(0xFF));
        printf("\n");
    }

    // CMD0 → idle state (R1 = 0x01). 0xFF here = card not talking at all (MISO stuck high:
    // wiring/power/absent card). 0x00 = responds but not idle — usually a warm (not cold-
    // power-cycled) card; re-sending GO_IDLE_STATE a few times forces it to idle.
    uint8_t r0 = 0xFF;
    for (int t = 0; t < 10; t++) {
        r0 = sd_cmd(0, 0);
        if (r0 == 0x01) break;
        sleep_ms(2);
    }
    if (s_verbose) printf("sd:   CMD0 R1=0x%02X (want 0x01; 0xFF=no MISO response)\n", r0);
    if (r0 != 0x01) { sd_cs(false); sd_xfer(0xFF); return false; }

    // CMD8 → interface condition. 0x01 = v2 card (reads 4-byte R7 trailer); 0x05 = v1.
    bool v2 = false;
    uint8_t r8 = sd_cmd(8, 0x1AA);
    if ((r8 & 0x04) == 0) {                          // command accepted → v2
        uint8_t r7[4];
        for (int i = 0; i < 4; i++) r7[i] = sd_xfer(0xFF);
        if (s_verbose) printf("sd:   CMD8 R1=0x%02X echo=%02X %02X (want ..01 AA)\n", r8, r7[2], r7[3]);
        if (r7[2] == 0x01 && r7[3] == 0xAA) v2 = true;
        else { sd_cs(false); sd_xfer(0xFF); return false; }   // bad voltage window
    } else if (s_verbose) {
        printf("sd:   CMD8 R1=0x%02X (v1 card / no CMD8)\n", r8);
    }

    // ACMD41 (with HCS for v2) until the card leaves idle (R1 = 0x00).
    absolute_time_t end = make_timeout_time_ms(1500);
    uint8_t r;
    int tries = 0;
    do {
        r = sd_acmd(41, v2 ? 0x40000000u : 0);
        tries++;
        if (r == 0x00) break;
    } while (!time_reached(end));
    if (s_verbose) printf("sd:   ACMD41 R1=0x%02X after %d tries (want 0x00)\n", r, tries);
    if (r != 0x00) { sd_cs(false); sd_xfer(0xFF); return false; }

    // CMD58 → OCR; CCS bit (bit 30) = block addressing (SDHC/SDXC).
    if (v2) {
        if (sd_cmd(58, 0) == 0x00) {
            uint8_t ocr[4];
            for (int i = 0; i < 4; i++) ocr[i] = sd_xfer(0xFF);
            s_sdhc = (ocr[0] & 0x40) != 0;
        }
    }
    // Byte-addressed cards: force 512-byte blocks.
    if (!s_sdhc) sd_cmd(16, 512);

    sd_cs(false);
    sd_xfer(0xFF);

    s_slow  = false;    // identify done → fast clock for data
    s_ready = true;
    return true;
}

// Wait for a data token, then read `len` bytes + 2 CRC bytes. CS must already be asserted.
static bool sd_read_data(uint8_t *dst, int len) {
    uint8_t tok = 0xFF;
    for (uint32_t i = 0; i < 200u * kXfersPerMs && tok == 0xFF; i++) tok = sd_xfer(0xFF);
    if (tok != 0xFE) return false;                  // 0xFE = start-of-data token
    for (int i = 0; i < len; i++) dst[i] = sd_xfer(0xFF);
    sd_xfer(0xFF); sd_xfer(0xFF);                   // discard CRC16
    return true;
}

bool sd_read_block(uint32_t lba, uint8_t *dst) {
    if (!s_ready) return false;
    uint32_t addr = s_sdhc ? lba : lba * 512u;      // SDHC = block address, SDSC = byte
    sd_cs(true);
    bool ok = (sd_cmd(17, addr) == 0x00) && sd_read_data(dst, 512);
    sd_cs(false);
    sd_xfer(0xFF);
    return ok;
}

// Write `n` consecutive sectors with CMD25 (multi-block write). One code path for every write,
// n = 1 included, so the path USB drive mode exercises on day one is the same one FatFs will use.
//
// Per block: 0xFC start token, 512 bytes, 2 dummy CRC bytes (SPI mode ignores CRC16 unless
// CMD59 enables it), then the DATA RESPONSE token -- low nibble 0b0101 = accepted, 0b1011 = CRC
// error, 0b1101 = write error -- then the card holds DO low while it programs. 0xFD ends the
// transfer, followed by one more busy period. ⚠ The busy waits are the long ones: the spec allows
// 250 ms per block, so a whole write can take a while and this blocks the caller throughout.
bool sd_write_blocks(uint32_t lba, const uint8_t *src, uint32_t n) {
    if (!s_ready || n == 0) return false;
    uint32_t addr = s_sdhc ? lba : lba * 512u;
    bool ok = false;
    sd_cs(true);
    if (sd_cmd(25, addr) == 0x00) {
        ok = true;
        for (uint32_t b = 0; b < n && ok; b++) {
            sd_xfer(0xFF);                          // >= 1 byte gap before each token
            sd_xfer(0xFC);
            for (int i = 0; i < 512; i++) sd_xfer(src[b * 512u + i]);
            sd_xfer(0xFF); sd_xfer(0xFF);           // CRC16, ignored
            uint8_t resp = sd_xfer(0xFF);
            if ((resp & 0x1F) != 0x05) { ok = false; break; }
            if (!sd_wait_ready(500)) { ok = false; break; }
        }
        sd_xfer(0xFF);
        sd_xfer(0xFD);                              // stop transmission
        sd_xfer(0xFF);
        if (!sd_wait_ready(500)) ok = false;
    }
    sd_cs(false);
    sd_xfer(0xFF);
    return ok;
}

uint32_t sd_sector_count(void) {
    if (!s_ready) return 0;
    uint8_t csd[16];
    sd_cs(true);
    bool ok = (sd_cmd(9, 0) == 0x00) && sd_read_data(csd, 16);
    sd_cs(false);
    sd_xfer(0xFF);
    if (!ok) return 0;

    if ((csd[0] >> 6) == 1) {                       // CSD v2 (SDHC/SDXC)
        uint32_t c_size = ((uint32_t)(csd[7] & 0x3F) << 16) | ((uint32_t)csd[8] << 8) | csd[9];
        return (c_size + 1) * 1024u;                // (C_SIZE+1) * 512 KB / 512 B
    } else {                                        // CSD v1 (SDSC)
        uint32_t read_bl_len = csd[5] & 0x0F;
        uint32_t c_size      = ((uint32_t)(csd[6] & 0x03) << 10) | ((uint32_t)csd[7] << 2) | (csd[8] >> 6);
        uint32_t c_size_mult = ((uint32_t)(csd[9] & 0x03) << 1) | (csd[10] >> 7);
        uint32_t blocks      = (c_size + 1) << (c_size_mult + 2);
        uint32_t block_len   = 1u << read_bl_len;
        return blocks * (block_len / 512u);
    }
}
