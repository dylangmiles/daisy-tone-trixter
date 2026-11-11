// audio/usb_audio.cpp — start + status for the composite Audio+CDC USB device. See usb_audio.h.
//
// ⚠ Started on libDaisy's OWN hUsbDeviceFS (not a private handle), with the composite class +
// descriptor, and the CDC interface registered with libDaisy's fops — so CDC_Transmit_FS, the logger
// and UsbRx target this device and the console + command set work unchanged. We do NOT call StartLog
// (that would init plain CDC on the same handle and the same port). The FS FIFO is re-carved here for
// the composite endpoint map, which the default libDaisy allocation has no room for.
#include "usb_audio.h"
#include "tt_audio_stream.h"
#include "usbd_core.h"
#include "usbd_cdc.h"          // USBD_CDC_RegisterInterface, USBD_CDC_ItfTypeDef
#include "stm32h7xx_hal.h"
#include <stdio.h>

extern "C" USBD_DescriptorsTypeDef TT_Composite_Desc;      // usbd_desc_composite.c
extern "C" USBD_ClassTypeDef       USBD_TT_Composite;       // usbd_composite.c
extern "C" USBD_CDC_ItfTypeDef     USBD_Interface_fops_FS;   // libDaisy's CDC interface callbacks
extern "C" USBD_HandleTypeDef      hUsbDeviceFS;             // libDaisy's device handle (CDC_Transmit_FS target)
extern "C" PCD_HandleTypeDef       hpcd_USB_OTG_FS;

// ⚠ Composite FS FIFO map, in 32-bit words (total ≤ 320 = the FS core's 1.25 KB). Audio TxFIFO3 is
// sized for the 24-bit worst case (50 samples × 4 ch × 3 B = 600 B ≤ 608). Re-called in order after
// USBD_Init so each SetTxFiFo recomputes its start address from the live registers (HAL behaviour).
//   Rx 0x50(80)  Tx0 0x20(32)  Tx1/CDC-in 0x20(32)  Tx2/CDC-cmd 0x10(16)  Tx3/audio 0x98(152) = 312
static void RecarveFifo(void)
{
    HAL_PCDEx_SetRxFiFo(&hpcd_USB_OTG_FS, 0x50);
    HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 0, 0x20);   // EP0 control
    HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 1, 0x20);   // EP1 IN  = CDC data   (0x81)
    HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 2, 0x10);   // EP2 IN  = CDC cmd    (0x82)
    HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 3, 0x98);   // EP3 IN  = audio iso  (0x83)
}

bool usb_audio_start(void)
{
    if(USBD_Init(&hUsbDeviceFS, &TT_Composite_Desc, DEVICE_FS) != USBD_OK)
        return false;
    if(USBD_RegisterClass(&hUsbDeviceFS, &USBD_TT_Composite) != USBD_OK)
        return false;
    if(USBD_CDC_RegisterInterface(&hUsbDeviceFS, &USBD_Interface_fops_FS) != USBD_OK)
        return false;
    RecarveFifo();                       // ⚠ after Init (default alloc done), before Start
    if(USBD_Start(&hUsbDeviceFS) != USBD_OK)
        return false;
    HAL_PWREx_EnableUSBVoltageDetector();
    __HAL_RCC_USB2_OTG_FS_ULPI_CLK_SLEEP_DISABLE();
    __HAL_RCC_USB1_OTG_HS_ULPI_CLK_SLEEP_DISABLE();
    // ⚠ libDaisy inits with Sof_enable = DISABLE, so the class SOF callback never fires. Enable the
    // SOF interrupt so we can count host frames (diagnostic: host frame rate vs iso-IN transmits).
    hpcd_USB_OTG_FS.Instance->GINTMSK |= USB_OTG_GINTMSK_SOFM;
    return true;
}

bool     usb_audio_configured(void) { return hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED; }
bool     usb_audio_streaming(void)  { return tt_audio_streaming() != 0; }
uint32_t usb_audio_frames(void)     { return tt_audio_frames(); }
uint32_t usb_audio_underruns(void)  { return tt_audio_underruns(); }
uint32_t usb_audio_overruns(void)   { return tt_audio_overruns(); }

void usb_audio_diag(char *out, int cap)
{
    snprintf(out, cap, "st%d strm%d fr%lu sof%lu un%lu ov%lu ii%lu rc%lu fill%lu",
             (int)hUsbDeviceFS.dev_state, (int)tt_audio_streaming(),
             (unsigned long)tt_audio_frames(), (unsigned long)tt_audio_sof_count(),
             (unsigned long)tt_audio_underruns(), (unsigned long)tt_audio_overruns(),
             (unsigned long)tt_audio_isoincomp(), (unsigned long)tt_audio_recenter(),
             (unsigned long)tt_audio_ring_fill());
}

// Machine-parseable USB-health line for the studio panel: cumulative counters (studio deltas them for
// rates) + per-interval packet histogram and fill range (reset on each read). Prefix "usbstat ".
void usb_audio_stat(char *out, int cap)
{
    uint32_t h[4], fmn, fmx;
    tt_audio_hist_take(h);
    tt_audio_fill_range_take(&fmn, &fmx);
    snprintf(out, cap,
             "usbstat fr=%lu sof=%lu ov=%lu un=%lu ii=%lu rc=%lu fill=%lu fmin=%lu fmax=%lu p47=%lu p48=%lu p49=%lu p50=%lu",
             (unsigned long)tt_audio_frames(), (unsigned long)tt_audio_sof_count(),
             (unsigned long)tt_audio_overruns(), (unsigned long)tt_audio_underruns(),
             (unsigned long)tt_audio_isoincomp(), (unsigned long)tt_audio_recenter(),
             (unsigned long)tt_audio_ring_fill(), (unsigned long)fmn, (unsigned long)fmx,
             (unsigned long)h[0], (unsigned long)h[1], (unsigned long)h[2], (unsigned long)h[3]);
}
