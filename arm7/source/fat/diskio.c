/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for FatFs     (C)ChaN, 2016        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include <nds.h>
#include <nds/disc_io.h>
#include "ff.h"			/* Obtains integer types */
#include "diskio.h"		/* Declarations of disk functions */

/* Definitions of physical drive number for each drive */
#define DEV_FAT		0 //dldi
#define DEV_SD		1 //dsi sd


/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

 DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	return 0;
}

extern FN_MEDIUM_STARTUP _DLDI_startup_ptr;
extern FN_MEDIUM_READSECTORS _DLDI_readSectors_ptr;
extern FN_MEDIUM_WRITESECTORS _DLDI_writeSectors_ptr;

/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

int sdmmc_sd_startup(void);

 DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
	if (pdrv == DEV_FAT)
	{
		return _DLDI_startup_ptr() ? 0 : STA_NOINIT;
	}
	else if (isDSiMode() && pdrv == DEV_SD)
	{
		return sdmmc_sd_startup() == 0 ? 0 : STA_NOINIT;
	}

	return STA_NOINIT;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

 DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	DWORD sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
	if (pdrv == DEV_FAT)
	{
		return _DLDI_readSectors_ptr(sector, count, buff) ? RES_OK : RES_ERROR;
	}
	else if (isDSiMode() && pdrv == DEV_SD)
	{
		return sdmmc_sdcard_readsectors(sector, count, buff) == 0 ? RES_OK : RES_ERROR;
	}

	return RES_PARERR;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

 DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	DWORD sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
    if (!count || !buff) return RES_PARERR;
    if (pdrv == DEV_FAT)
        return _DLDI_writeSectors_ptr(sector, count, buff) ? RES_OK : RES_ERROR;
    if (isDSiMode() && pdrv == DEV_SD)
        return sdmmc_sdcard_writesectors(sector, count, (void*)buff) == 0 ? RES_OK : RES_ERROR;
    return RES_PARERR;
}

#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

 DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
    if (pdrv != DEV_FAT && !(isDSiMode() && pdrv == DEV_SD)) return RES_PARERR;
    // Both backends complete sector writes before returning.
    if (cmd == CTRL_SYNC) return RES_OK;
    if (cmd == GET_SECTOR_SIZE && buff) { *(WORD*)buff = 512; return RES_OK; }
    if (cmd == GET_BLOCK_SIZE && buff) { *(DWORD*)buff = 1; return RES_OK; }
    return RES_PARERR;
}

