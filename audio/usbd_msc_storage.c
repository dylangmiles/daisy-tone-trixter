// audio/usbd_msc_storage.c — the MSC class's view of the SD card, for USB DRIVE MODE.
//
// The ST MSC middleware turns SCSI READ(10)/WRITE(10) into calls on this table, ONE 512-byte block
// at a time (MSC_MEDIA_PACKET), from inside the OTG_FS interrupt. Everything here therefore has to
// be interrupt-safe: no clock-based timeouts (sd_spi.c is iteration-bounded for exactly this), no
// printf, no waiting on anything the ISR would block.
//
// ⚠ Raw block access. The host's filesystem driver owns the card while this is running; nothing on
// the pedal may have FatFs mounted at the same time. main.cpp enters drive mode BEFORE f_mount and
// the only way out is a power cycle -- see UsbDriveMode().
#include "usbd_msc.h"
#include "sd_spi.h"

// Activity counters for the panel: the only feedback the builder gets that the host is talking.
volatile uint32_t g_msc_reads  = 0;
volatile uint32_t g_msc_writes = 0;
volatile uint32_t g_msc_errors = 0;

static int8_t STORAGE_Init(uint8_t lun)
{
    (void)lun;
    return sd_is_ready() ? 0 : -1;
}

static int8_t STORAGE_GetCapacity(uint8_t lun, uint32_t *block_num, uint16_t *block_size)
{
    (void)lun;
    *block_num  = sd_sector_count();
    *block_size = 512;
    return (*block_num != 0) ? 0 : -1;
}

static int8_t STORAGE_IsReady(uint8_t lun)
{
    (void)lun;
    return sd_is_ready() ? 0 : -1;
}

static int8_t STORAGE_IsWriteProtected(uint8_t lun)
{
    (void)lun;
    return 0;
}

static int8_t STORAGE_Read(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len)
{
    (void)lun;
    for (uint16_t i = 0; i < blk_len; i++)
    {
        if (!sd_read_block(blk_addr + i, buf + 512u * i))
        {
            g_msc_errors++;
            return -1;
        }
    }
    g_msc_reads += blk_len;
    return 0;
}

static int8_t STORAGE_Write(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len)
{
    (void)lun;
    if (!sd_write_blocks(blk_addr, buf, blk_len))
    {
        g_msc_errors++;
        return -1;
    }
    g_msc_writes += blk_len;
    return 0;
}

static int8_t STORAGE_GetMaxLun(void)
{
    return 0;   // one LUN
}

// Standard INQUIRY response, 36 bytes. Vendor/product are what the host shows in its device list.
static int8_t STORAGE_Inquirydata[] = {
    0x00,                              // direct-access block device
    0x80,                              // removable
    0x02, 0x02,                        // SCSI-2 / response format
    (STANDARD_INQUIRY_DATA_LEN - 5),
    0x00, 0x00, 0x00,
    'T', 'o', 'n', 'e', ' ', 'T', 'r', 'x',                       // vendor,  8 bytes
    'T', 'r', 'i', 'x', 't', 'e', 'r', ' ', 'S', 'D', ' ', 'C', 'a', 'r', 'd', ' ',  // product, 16
    '1', '.', '0', '0',                                           // revision, 4
};

USBD_StorageTypeDef USBD_TT_Storage = {
    STORAGE_Init,
    STORAGE_GetCapacity,
    STORAGE_IsReady,
    STORAGE_IsWriteProtected,
    STORAGE_Read,
    STORAGE_Write,
    STORAGE_GetMaxLun,
    STORAGE_Inquirydata,
};
