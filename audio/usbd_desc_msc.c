// audio/usbd_desc_msc.c — USB descriptors for USB DRIVE MODE.
//
// libDaisy's own descriptor set (src/usbd/usbd_desc.c) declares the DEVICE as class 2 (CDC) and
// calls itself "Daisy Seed Built In". A mass-storage configuration under a CDC device class is
// legal but confuses hosts that cache device identity by VID:PID, so drive mode gets its own set:
// device class 0 (class is per-interface, as MSC expects), a different PID, and a product string
// the builder will recognise in Finder / Disk Utility.
#include "usbd_core.h"
#include "usbd_def.h"
#include "stm32h7xx_hal.h"

#define TT_USBD_VID              0x0483   // STMicroelectronics, as libDaisy uses
#define TT_USBD_PID              0x5750   // distinct from the CDC device (0x5740)
#define TT_USBD_LANGID           1033
#define TT_MANUFACTURER_STRING   "Tone Trixter"
#define TT_PRODUCT_STRING        "Tone Trixter SD Card"
#define TT_CONFIG_STRING         "SD card"
#define TT_INTERFACE_STRING      "Mass storage"
#define TT_SERIAL_LEN            0x1A

__ALIGN_BEGIN static uint8_t TT_DeviceDesc[USB_LEN_DEV_DESC] __ALIGN_END = {
    0x12, USB_DESC_TYPE_DEVICE,
    0x00, 0x02,                      // bcdUSB 2.00
    0x00, 0x00, 0x00,                // class / subclass / protocol: per interface
    USB_MAX_EP0_SIZE,
    LOBYTE(TT_USBD_VID), HIBYTE(TT_USBD_VID),
    LOBYTE(TT_USBD_PID), HIBYTE(TT_USBD_PID),
    0x00, 0x02,                      // bcdDevice 2.00
    USBD_IDX_MFC_STR, USBD_IDX_PRODUCT_STR, USBD_IDX_SERIAL_STR,
    USBD_MAX_NUM_CONFIGURATION,
};

__ALIGN_BEGIN static uint8_t TT_LangIDDesc[USB_LEN_LANGID_STR_DESC] __ALIGN_END = {
    USB_LEN_LANGID_STR_DESC, USB_DESC_TYPE_STRING, LOBYTE(TT_USBD_LANGID), HIBYTE(TT_USBD_LANGID),
};

__ALIGN_BEGIN static uint8_t TT_StrDesc[USBD_MAX_STR_DESC_SIZ] __ALIGN_END;
__ALIGN_BEGIN static uint8_t TT_SerialDesc[TT_SERIAL_LEN] __ALIGN_END = {
    TT_SERIAL_LEN, USB_DESC_TYPE_STRING,
};

static void IntToUnicode(uint32_t value, uint8_t *pbuf, uint8_t len)
{
    for (uint8_t i = 0; i < len; i++)
    {
        uint8_t nib = (uint8_t)(value >> 28);
        pbuf[2 * i]     = (uint8_t)(nib < 0xA ? nib + '0' : nib + 'A' - 10);
        pbuf[2 * i + 1] = 0;
        value <<= 4;
    }
}

// The chip's unique ID, so the host sees the same "disk" every time this board is plugged in.
static void GetSerial(void)
{
    uint32_t d0 = HAL_GetUIDw0(), d1 = HAL_GetUIDw1(), d2 = HAL_GetUIDw2();
    d0 += d2;
    IntToUnicode(d0, &TT_SerialDesc[2], 8);
    IntToUnicode(d1, &TT_SerialDesc[18], 4);
}

static uint8_t *DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed; *length = sizeof(TT_DeviceDesc); return TT_DeviceDesc;
}
static uint8_t *LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed; *length = sizeof(TT_LangIDDesc); return TT_LangIDDesc;
}
static uint8_t *ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed; USBD_GetString((uint8_t *)TT_MANUFACTURER_STRING, TT_StrDesc, length); return TT_StrDesc;
}
static uint8_t *ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed; USBD_GetString((uint8_t *)TT_PRODUCT_STRING, TT_StrDesc, length); return TT_StrDesc;
}
static uint8_t *SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed; *length = TT_SERIAL_LEN; GetSerial(); return TT_SerialDesc;
}
static uint8_t *ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed; USBD_GetString((uint8_t *)TT_CONFIG_STRING, TT_StrDesc, length); return TT_StrDesc;
}
static uint8_t *InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed; USBD_GetString((uint8_t *)TT_INTERFACE_STRING, TT_StrDesc, length); return TT_StrDesc;
}

USBD_DescriptorsTypeDef TT_MSC_Desc = {
    DeviceDescriptor,
    LangIDStrDescriptor,
    ManufacturerStrDescriptor,
    ProductStrDescriptor,
    SerialStrDescriptor,
    ConfigStrDescriptor,
    InterfaceStrDescriptor,
};
