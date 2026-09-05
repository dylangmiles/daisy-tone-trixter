// audio/sd_diskio.c — FatFs disk I/O for the bit-banged SD driver (sd_spi.c).
//
// ⚠ REGISTERED DRIVER, not the classic diskio API the Pico build uses.
//
// libDaisy's own diskio.c already defines disk_read / disk_write / disk_status / disk_ioctl and
// dispatches them to drivers registered through ff_gen_drv. Defining those symbols here as well
// would be a duplicate-symbol collision the moment anything pulled libDaisy's diskio.o in. So this
// exposes a Diskio_drvTypeDef instead and hands it to FATFS_LinkDriver().
//
// ⚠ We cannot use libDaisy's FatFSInterface either: it only links its own SDMMC peripheral driver
// or USB host, and SDMMC is impossible on this board — the Daisy's SDMMC CMD line is D5, which is
// wired to the microSD module's GND (row 15, cut and jumpered). Hence the bit-bang.
//
// Read-only, single volume, 512-byte sectors.

#include "ff.h"
#include "ff_gen_drv.h"
#include "audio/sd_spi.h"

#include <stddef.h>
#include <stdint.h>

static DSTATUS tt_sd_status(BYTE lun)
{
    (void)lun;
    return sd_is_ready() ? 0 : STA_NOINIT;
}

static DSTATUS tt_sd_initialize(BYTE lun)
{
    (void)lun;
    return sd_init() ? 0 : STA_NOINIT;
}

static DRESULT tt_sd_read(BYTE lun, BYTE *buff, DWORD sector, UINT count)
{
    (void)lun;
    for(UINT i = 0; i < count; i++)
        if(!sd_read_block((uint32_t)sector + i, buff + (size_t)i * 512))
            return RES_ERROR;
    return RES_OK;
}

#if _USE_WRITE == 1
// ⚠ Read-only by design: the card holds presets and IRs, and nothing on the audio path has any
// business writing to it. Returning RES_WRPRT makes an accidental write fail loudly.
static DRESULT tt_sd_write(BYTE lun, const BYTE *buff, DWORD sector, UINT count)
{
    (void)lun; (void)buff; (void)sector; (void)count;
    return RES_WRPRT;
}
#endif

#if _USE_IOCTL == 1
static DRESULT tt_sd_ioctl(BYTE lun, BYTE cmd, void *buff)
{
    (void)lun;
    switch(cmd)
    {
        case CTRL_SYNC:        return RES_OK;
        case GET_SECTOR_COUNT: *(DWORD *)buff = sd_sector_count(); return RES_OK;
        case GET_SECTOR_SIZE:  *(WORD  *)buff = 512;               return RES_OK;
        case GET_BLOCK_SIZE:   *(DWORD *)buff = 1;                 return RES_OK;
        default:               return RES_PARERR;
    }
}
#endif

const Diskio_drvTypeDef tt_sd_driver = {
    tt_sd_initialize,
    tt_sd_status,
    tt_sd_read,
#if _USE_WRITE == 1
    tt_sd_write,
#endif
#if _USE_IOCTL == 1
    tt_sd_ioctl,
#endif
};
