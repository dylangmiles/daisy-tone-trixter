// audio/sd_sdmmc.c — SD card over the STM32H750's NATIVE SDMMC1 peripheral (4-bit SD bus).
//
// ✅ THE DEFAULT SD DRIVER since 2026-11-27 (bench-verified: 4-bit / 25 MHz, ~6 MB/s, Mac auto-mount,
// 0 backing underruns, cold boots + `sdpower cycle` OK). `make SD_SDMMC=0` falls back to sd_spi.c.
// Same API as sd_spi.h, so FatFs (sd_diskio.c), USB drive mode (usbd_msc_storage.c) and main.cpp
// don't care which driver is linked.
//
// WHY: the bit-bang SPI driver tops out at ~166 KB/s. That is too slow to record on the pedal
// (stereo 24/48 = 288 KB/s), and it is why macOS 26 gives up auto-mounting the card in drive mode
// (FSKit reads the whole FAT within a ~30 s timeout, and 7.7 MB at ~133 KB/s takes ~58 s).
// The Rev A PCB already routes all six SD lines to SDMMC1: Daisy D1..D6 =
//   D1 PC11 DAT3 · D2 PC10 DAT2 · D3 PC9 DAT1 · D4 PC8 DAT0 · D5 PD2 CMD · D6 PC12 CLK
// (checked against daisy/pcb/tone_trixter.kicad_pcb, U4 pads 2..7 / J10). The old header comments
// claiming "SDMMC is impossible on this board" refer to the SUPERSEDED perfboard, where D5 was tied
// to GND. They are not true for the Rev A PCB.
//
// Design choices:
//  - We use libDaisy's `hsd1` handle and its HAL_SD_MspInit (pins AF12 + SDMMC1 clock, from
//    libdaisy.a/per/sdmmc.o), but drive the HAL ourselves, NOT through FatFSInterface. That keeps
//    our own registered diskio (sd_diskio.c) and the MSC storage table unchanged.
//  - POLLED transfers (own loop modelled on HAL_SD_ReadBlocks / WriteBlocks), no DMA. The CPU copies the FIFO, so
//    buffers can live in any RAM (IDMA can't reach DTCM) and there is no D-cache maintenance.
//    Polled mode is also what the MSC path needs, because it calls us from inside the OTG ISR.
//  - HARDWARE FLOW CONTROL ON. The foreground FatFs read is preempted by the audio callback; with
//    HWFC the SDMMC stops the card clock when its FIFO fills, instead of raising RXOVERR / TXUNDERR.
//  - INTERNAL PULL-UPS on CMD + DAT0..3. The bare DM3AT has no external pull-ups (see `sdpu` in
//    main.cpp). DAT3 MUST be high at CMD0, or the card drops into SPI mode and SD mode is lost
//    until the next VDD power cycle.
//
// ⚠ A card stays in whatever mode its first CMD0 put it in until VDD is cycled. Never run the
// bit-bang diagnostics (TestSd's CMD0, sdreset, sdmiso, sdscope) in this build. main.cpp gates
// them on TT_SD_SDMMC.
//
// ISR safety (drive mode): the data path is our own polled loop (sd_xfer_blocks), bounded by the
// SDMMC's hardware data timer plus an iteration cap, never by SysTick. A dead or pulled card fails the
// transfer (the host sees a read error) instead of hanging. sd_init() does use HAL_Delay, so it's
// foreground only, which main.cpp guarantees.

#include "audio/sd_spi.h"
#include "stm32h7xx_hal.h"
#include <stdio.h>

extern SD_HandleTypeDef hsd1;   // defined in libDaisy src/per/sdmmc.cpp (+ HAL_SD_MspInit, IRQ)

#ifndef TT_SDMMC_CLKDIV
#define TT_SDMMC_CLKDIV 4       // SDMMC_CK = 200 MHz / (2*div): 4 = 25 MHz (Default Speed), 8 = 12.5
#endif
#ifndef TT_SDMMC_1BIT
#define TT_SDMMC_1BIT 0         // 1 = 1-bit bus (DAT0 only) for bring-up
#endif

static bool     s_ready   = false;
static bool     s_verbose = false;
static uint32_t s_err     = 0;   // last HAL error code, for the verbose trace

bool sd_is_ready(void)      { return s_ready; }
bool sd_is_sdhc(void)       { return s_ready && hsd1.SdCard.CardType == CARD_SDHC_SDXC; }
void sd_set_offline(void)   { s_ready = false; }
void sd_set_verbose(bool v) { s_verbose = v; }

// Pull-up (PUPDR = 01) on CMD (PD2) and DAT0..3 (PC8..11). Called AFTER HAL_SD_MspInit, which
// configures them GPIO_NOPULL. The mode stays AF; only the pull changes.
static void sd_pullups(void)
{
    const uint32_t cpins[] = {8, 9, 10, 11};
    for (unsigned i = 0; i < 4; i++) {
        uint32_t p = cpins[i];
        GPIOC->PUPDR = (GPIOC->PUPDR & ~(3u << (2 * p))) | (1u << (2 * p));
    }
    GPIOD->PUPDR = (GPIOD->PUPDR & ~(3u << (2 * 2))) | (1u << (2 * 2));
}

// Wait for the card to leave PROGRAMMING/RECEIVING (busy after a write). Iteration-bounded: CMD13
// uses the command-path timeout, which is a software count rather than SysTick, so this is safe
// in the ISR.
static bool sd_wait_transfer(void)
{
    for (uint32_t i = 0; i < 200000u; i++)
        if (HAL_SD_GetCardState(&hsd1) == HAL_SD_CARD_TRANSFER)
            return true;
    return false;
}

bool sd_init(void)
{
    s_ready = false;

    // Start from scratch every time: a re-insert or VDD power cycle needs a full identify.
    // DeInit puts the pins back to analog and resets the HAL state, so MspInit runs again below.
    if (hsd1.State != HAL_SD_STATE_RESET)
        HAL_SD_DeInit(&hsd1);

    hsd1.Instance                 = SDMMC1;
    hsd1.Init.ClockEdge           = SDMMC_CLOCK_EDGE_RISING;
    hsd1.Init.ClockPowerSave      = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    hsd1.Init.BusWide             = SDMMC_BUS_WIDE_4B;   // MspInit only AFs DAT1..3 when 4B
    hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
    hsd1.Init.ClockDiv            = TT_SDMMC_CLKDIV;

    // Do the MSP ourselves so the pull-ups can go on BEFORE the card sees CMD0. Then mark the handle
    // non-RESET so HAL_SD_Init doesn't redo the MSP (which would put the pins back to NOPULL).
    HAL_SD_MspInit(&hsd1);
    sd_pullups();
    HAL_Delay(2);                                         // let the ~40k pull-ups settle
    hsd1.Lock  = HAL_UNLOCKED;
    hsd1.State = HAL_SD_STATE_READY;

#if TT_SDMMC_1BIT
    hsd1.Init.BusWide = SDMMC_BUS_WIDE_1B;                // pins stay AF + pulled up; bus uses DAT0
#endif

    // HAL_SD_Init = identify at 400 kHz (1-bit) → CMD2/3/9/7 → ACMD13 → ACMD6 (bus width) → user clock.
    // ⚠ It calls HAL_Delay, so foreground only (never from the MSC ISR). main.cpp already guarantees that.
    if (HAL_SD_Init(&hsd1) != HAL_OK) {
        s_err = hsd1.ErrorCode;
        if (s_verbose) printf("sd(sdmmc): HAL_SD_Init FAILED, ErrorCode=0x%08lX\n", (unsigned long)s_err);
        return false;
    }
    if (!sd_wait_transfer()) {
        if (s_verbose) printf("sd(sdmmc): card never reached TRANSFER state\n");
        return false;
    }

    if (s_verbose) {
        HAL_SD_CardInfoTypeDef ci;
        HAL_SD_GetCardInfo(&hsd1, &ci);
        printf("sd(sdmmc): OK type=%s speed=%lu blocks=%lu (%lu MB) bus=%s div=%d\n",
               ci.CardType == CARD_SDHC_SDXC ? "SDHC/XC" : "SDSC",
               (unsigned long)ci.CardSpeed, (unsigned long)ci.LogBlockNbr,
               (unsigned long)(ci.LogBlockNbr / 2048u),
               hsd1.Init.BusWide == SDMMC_BUS_WIDE_4B ? "4-bit" : "1-bit", TT_SDMMC_CLKDIV);
    }
    s_ready = true;
    return true;
}

// ---------------------------------------------------------------- ISR-safe polled data path
// Our own version of HAL_SD_ReadBlocks / HAL_SD_WriteBlocks (same register sequence). We need it
// because the HAL's data path can't be bounded inside the OTG ISR, where USB drive mode calls us:
//   - HAL sets DataTimeOut = 0xFFFFFFFF card clocks (~171 s at 25 MHz), and
//   - its software timeout uses HAL_GetTick, which stops while SysTick is masked.
// So a dead or pulled card could hang drive mode until the IWDG fired. Here every transfer is bounded
// twice, neither by SysTick:
//   1. HARDWARE: DTIMER = kDataTimeoutMs of real card clocks (read latency / write busy), so the
//      SDMMC raises DTIMEOUT on its own.
//   2. SOFTWARE: an iteration cap on the poll loop, a backstop if the peripheral itself wedges.
#define kDataTimeoutMs 250u           // SD spec: 100 ms read access, 250 ms write busy (SDHC)
#define kPollCap       20000000u      // loop passes; ~>0.5 s of spinning at 400 MHz, never reached when healthy

// Card clocks per ms at the bus clock actually programmed (the HAL may have capped our divider).
static uint32_t sd_clocks_per_ms(void)
{
    uint32_t div = hsd1.Instance->CLKCR & SDMMC_CLKCR_CLKDIV;
    uint32_t ker = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SDMMC);
    uint32_t ck  = div ? ker / (2u * div) : ker;
    return ck / 1000u;
}

// n blocks at lba, either direction. Returns false and records s_err (HAL_SD_ERROR_* bits) on failure.
static bool sd_xfer_blocks(uint32_t lba, uint8_t *buf, uint32_t n, bool write)
{
    SDMMC_TypeDef *sd = hsd1.Instance;
    uint32_t addr = (hsd1.SdCard.CardType == CARD_SDHC_SDXC) ? lba : lba * 512u;
    if (lba + n > hsd1.SdCard.LogBlockNbr) { s_err = HAL_SD_ERROR_ADDR_OUT_OF_RANGE; return false; }

    sd->DCTRL = 0u;
    SDMMC_DataInitTypeDef cfg;
    cfg.DataTimeOut   = sd_clocks_per_ms() * kDataTimeoutMs;
    cfg.DataLength    = n * 512u;
    cfg.DataBlockSize = SDMMC_DATABLOCK_SIZE_512B;
    cfg.TransferDir   = write ? SDMMC_TRANSFER_DIR_TO_CARD : SDMMC_TRANSFER_DIR_TO_SDMMC;
    cfg.TransferMode  = SDMMC_TRANSFER_MODE_BLOCK;
    cfg.DPSM          = SDMMC_DPSM_DISABLE;
    (void)SDMMC_ConfigData(sd, &cfg);
    __SDMMC_CMDTRANS_ENABLE(sd);

    // Command path: SDMMC_GetCmdResp1 bounds itself with a software count (not SysTick) -> ISR-safe.
    uint32_t e = write ? (n > 1 ? SDMMC_CmdWriteMultiBlock(sd, addr) : SDMMC_CmdWriteSingleBlock(sd, addr))
                       : (n > 1 ? SDMMC_CmdReadMultiBlock(sd, addr)  : SDMMC_CmdReadSingleBlock(sd, addr));
    if (e != HAL_SD_ERROR_NONE) {
        __SDMMC_CLEAR_FLAG(sd, SDMMC_STATIC_FLAGS);
        __SDMMC_CMDTRANS_DISABLE(sd);
        s_err = e;
        return false;
    }

    const uint32_t done_mask = (write ? SDMMC_FLAG_TXUNDERR : SDMMC_FLAG_RXOVERR)
                             | SDMMC_FLAG_DCRCFAIL | SDMMC_FLAG_DTIMEOUT | SDMMC_FLAG_DATAEND;
    uint32_t remaining = n * 512u;
    uint32_t polls = 0;
    bool     capped = false;
    while (!(sd->STA & done_mask)) {
        if (++polls > kPollCap) { capped = true; break; }
        if (!write && (sd->STA & SDMMC_FLAG_RXFIFOHF) && remaining >= 32u) {
            for (int i = 0; i < 8; i++) {
                uint32_t d = SDMMC_ReadFIFO(sd);
                buf[0] = (uint8_t)d; buf[1] = (uint8_t)(d >> 8); buf[2] = (uint8_t)(d >> 16); buf[3] = (uint8_t)(d >> 24);
                buf += 4;
            }
            remaining -= 32u;
        } else if (write && (sd->STA & SDMMC_FLAG_TXFIFOHE) && remaining >= 32u) {
            for (int i = 0; i < 8; i++) {
                uint32_t d = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
                (void)SDMMC_WriteFIFO(sd, &d);
                buf += 4;
            }
            remaining -= 32u;
        }
    }
    __SDMMC_CMDTRANS_DISABLE(sd);

    uint32_t sta = sd->STA;
    bool ok = !capped && (sta & SDMMC_FLAG_DATAEND)
           && !(sta & (SDMMC_FLAG_DCRCFAIL | SDMMC_FLAG_DTIMEOUT | SDMMC_FLAG_RXOVERR | SDMMC_FLAG_TXUNDERR));
    if (!ok) {
        s_err = capped                     ? HAL_SD_ERROR_TIMEOUT
              : (sta & SDMMC_FLAG_DTIMEOUT) ? HAL_SD_ERROR_DATA_TIMEOUT
              : (sta & SDMMC_FLAG_DCRCFAIL) ? HAL_SD_ERROR_DATA_CRC_FAIL
              : (sta & SDMMC_FLAG_RXOVERR)  ? HAL_SD_ERROR_RX_OVERRUN
              :                               HAL_SD_ERROR_TX_UNDERRUN;
        if (capped) sd->DCTRL = 0u;        // abort the DPSM so the next transfer starts clean
    }
    // Multi-block always needs CMD12, success or not, to bring the card back to TRANSFER.
    if (n > 1) {
        uint32_t se = SDMMC_CmdStopTransfer(sd);
        if (ok && se != HAL_SD_ERROR_NONE) { ok = false; s_err = se; }
    }
    __SDMMC_CLEAR_FLAG(sd, SDMMC_STATIC_FLAGS);
    return ok;
}

static void sd_report(const char *what, uint32_t lba, uint32_t n)
{
    if (s_verbose) printf("sd(sdmmc): %s lba=%lu n=%lu FAILED err=0x%08lX\n",
                          what, (unsigned long)lba, (unsigned long)n, (unsigned long)s_err);
}

// Multi-block read (CMD18 when n > 1).
bool sd_read_blocks(uint32_t lba, uint8_t *dst, uint32_t n)
{
    if (!s_ready || n == 0) return false;
    if (!sd_wait_transfer()) { s_err = HAL_SD_ERROR_TIMEOUT; return false; }
    if (!sd_xfer_blocks(lba, dst, n, false)) { sd_report("read", lba, n); return false; }
    return true;
}

bool sd_read_block(uint32_t lba, uint8_t *dst)
{
    return sd_read_blocks(lba, dst, 1);
}

bool sd_write_blocks(uint32_t lba, const uint8_t *src, uint32_t n)
{
    if (!s_ready || n == 0) return false;
    if (!sd_wait_transfer()) { s_err = HAL_SD_ERROR_TIMEOUT; return false; }
    if (!sd_xfer_blocks(lba, (uint8_t *)src, n, true)) { sd_report("write", lba, n); return false; }
    return sd_wait_transfer();   // card programs after DATAEND; don't return until it's done
}

uint32_t sd_sector_count(void)
{
    return s_ready ? hsd1.SdCard.LogBlockNbr : 0;   // logical 512-byte blocks (SDSC-safe)
}

uint32_t sd_last_error(void) { return s_err; }

void sd_pin_check(void)
{
    printf("sd(sdmmc): sdpins is a bit-bang diagnostic and isn't available in the SDMMC build.\n");
}
