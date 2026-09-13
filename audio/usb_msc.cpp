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
    return USBD_Start(&hUsbMsc) == USBD_OK;
}

bool     usb_msc_configured(void) { return hUsbMsc.dev_state == USBD_STATE_CONFIGURED; }
uint32_t usb_msc_reads(void)      { return g_msc_reads; }
uint32_t usb_msc_writes(void)     { return g_msc_writes; }
uint32_t usb_msc_errors(void)     { return g_msc_errors; }
