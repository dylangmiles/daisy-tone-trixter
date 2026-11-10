// audio/usbd_desc_composite.c — USB device + string descriptors for the composite Audio+CDC device.
//
// ⚠ Device class = 0xEF / 0x02 / 0x01 (Miscellaneous, Interface Association Descriptor) — required
// for a multi-function (CDC + Audio) device so the host binds each function's class driver via the
// IADs in the config descriptor (usbd_composite.c). Own PID, distinct from CDC (0x5740), MSC drive
// (0x5750) and the Phase-1 audio-only (0x5751). Product = "Tone Trixie" (the pedal).
#include "usbd_core.h"
#include "usbd_def.h"
#include "stm32h7xx_hal.h"

#define TT_USBD_VID              0x0483
#define TT_USBD_PID              0x5753   // bumped for UAC2 (force macOS to re-read descriptors, not cached UAC1)
#define TT_USBD_LANGID           1033
#define TT_MANUFACTURER_STRING   "Dylan Miles"
#define TT_PRODUCT_STRING        "Tone Trixie"
#define TT_SERIAL_LEN            0x1A

__ALIGN_BEGIN static uint8_t TT_DeviceDesc[USB_LEN_DEV_DESC] __ALIGN_END = {
    0x12, USB_DESC_TYPE_DEVICE,
    0x00, 0x02,                      // bcdUSB 2.00
    0xEF, 0x02, 0x01,                // class/subclass/protocol: Misc / IAD composite
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
    for(uint8_t i = 0; i < len; i++)
    {
        uint8_t nib     = (uint8_t)(value >> 28);
        pbuf[2 * i]     = (uint8_t)(nib < 0xA ? nib + '0' : nib + 'A' - 10);
        pbuf[2 * i + 1] = 0;
        value <<= 4;
    }
}

static void GetSerial(void)
{
    uint32_t d0 = HAL_GetUIDw0(), d1 = HAL_GetUIDw1(), d2 = HAL_GetUIDw2();
    d0 += d2;
    IntToUnicode(d0, &TT_SerialDesc[2], 8);
    IntToUnicode(d1, &TT_SerialDesc[18], 4);
}

static uint8_t *DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{ (void)speed; *length = sizeof(TT_DeviceDesc); return TT_DeviceDesc; }
static uint8_t *LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{ (void)speed; *length = sizeof(TT_LangIDDesc); return TT_LangIDDesc; }
static uint8_t *ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{ (void)speed; USBD_GetString((uint8_t *)TT_MANUFACTURER_STRING, TT_StrDesc, length); return TT_StrDesc; }
static uint8_t *ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{ (void)speed; USBD_GetString((uint8_t *)TT_PRODUCT_STRING, TT_StrDesc, length); return TT_StrDesc; }
static uint8_t *SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{ (void)speed; *length = TT_SERIAL_LEN; GetSerial(); return TT_SerialDesc; }
static uint8_t *ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{ (void)speed; USBD_GetString((uint8_t *)TT_PRODUCT_STRING, TT_StrDesc, length); return TT_StrDesc; }
static uint8_t *InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{ (void)speed; USBD_GetString((uint8_t *)TT_PRODUCT_STRING, TT_StrDesc, length); return TT_StrDesc; }

USBD_DescriptorsTypeDef TT_Composite_Desc = {
    DeviceDescriptor,
    LangIDStrDescriptor,
    ManufacturerStrDescriptor,
    ProductStrDescriptor,
    SerialStrDescriptor,
    ConfigStrDescriptor,
    InterfaceStrDescriptor,
};
