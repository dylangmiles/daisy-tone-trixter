// audio/usb_msc.cpp — see usb_msc.h.
//
// Mirrors what libDaisy's UsbHandle::Init does for CDC (src/hid/usb.cpp), with the MSC class and
// our own descriptors in place of CDC's. libDaisy's usbd_conf.c supplies the low-level glue
// (USBD_LL_*, the PCD handle, the OTG_FS interrupt) keyed on the device id, so a second
// USBD_HandleTypeDef on DEVICE_FS is all that is needed -- provided the CDC logger was never
// started on the same port. main.cpp checks the boot gesture BEFORE StartLog for that reason.
#include "usb_msc.h"
#include "usbd_core.h"
#include "usbd_msc.h"
#include "stm32h7xx_hal.h"
#include <stdio.h>

extern "C" USBD_DescriptorsTypeDef TT_MSC_Desc;
extern "C" USBD_StorageTypeDef     USBD_TT_Storage;
extern "C" volatile uint32_t       g_msc_reads, g_msc_writes, g_msc_errors;

static USBD_HandleTypeDef hUsbMsc;

bool usb_msc_start(void)
{
    if(USBD_Init(&hUsbMsc, &TT_MSC_Desc, DEVICE_FS) != USBD_OK)
        return false;
    if(USBD_RegisterClass(&hUsbMsc, &USBD_MSC) != USBD_OK)
        return false;
    if(USBD_MSC_RegisterStorage(&hUsbMsc, &USBD_TT_Storage) != USBD_OK)
        return false;
    if(USBD_Start(&hUsbMsc) != USBD_OK)
        return false;
    // ⚠ THE H7's USB TRANSCEIVER NEEDS ITS SUPPLY DETECTOR ENABLED, or the PHY never pulls D+ up
    // and the host sees an empty port. libDaisy does this at the end of UsbHandle::Init; it is the
    // one line in that path that is not the USB device library, and it was the whole fault on
    // 2026-09-13: "host: waiting" with nothing on the Mac's USB tree.
    HAL_PWREx_EnableUSBVoltageDetector();
    return true;
}

bool     usb_msc_configured(void) { return hUsbMsc.dev_state == USBD_STATE_CONFIGURED; }

extern "C" PCD_HandleTypeDef hpcd_USB_OTG_FS;
void usb_msc_diag(char *out, int cap)
{
    USB_OTG_GlobalTypeDef *g = hpcd_USB_OTG_FS.Instance;
    if(!g) { snprintf(out, cap, "no pcd"); return; }
    USB_OTG_DeviceTypeDef *d = (USB_OTG_DeviceTypeDef *)((uint32_t)g + USB_OTG_DEVICE_BASE);
    snprintf(out, cap, "st%d vb%d fn%lu gi%lx", (int)hUsbMsc.dev_state,
             (int)((g->GOTGCTL & USB_OTG_GOTGCTL_BSESVLD) ? 1 : 0),
             (unsigned long)((d->DSTS >> 8) & 0x3FFF), (unsigned long)(g->GINTSTS & 0xFFFF));
}

// Second line: DCTL (bit1 = SDIS soft-disconnect -- must be 0), GCCFG (bit16 PWRDWN = transceiver
// powered, bit21 VBDEN), GUSBCFG (bit30 FDMOD forced device), and the PWR USB33 detector/ready bits.
void usb_msc_diag2(char *out, int cap)
{
    USB_OTG_GlobalTypeDef *g = hpcd_USB_OTG_FS.Instance;
    if(!g) { snprintf(out, cap, "no pcd"); return; }
    USB_OTG_DeviceTypeDef *d = (USB_OTG_DeviceTypeDef *)((uint32_t)g + USB_OTG_DEVICE_BASE);
    snprintf(out, cap, "dc%lx gc%lx u%lx p%lx", (unsigned long)(d->DCTL & 0xFF),
             (unsigned long)(g->GCCFG >> 16), (unsigned long)(g->GUSBCFG >> 28),
             (unsigned long)((PWR->CR3 >> 24) & 0xF));
}
uint32_t usb_msc_reads(void)      { return g_msc_reads; }
uint32_t usb_msc_writes(void)     { return g_msc_writes; }
uint32_t usb_msc_errors(void)     { return g_msc_errors; }
