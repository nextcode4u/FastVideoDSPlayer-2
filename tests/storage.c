#include <nds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../arm7/source/fat/ff.h"
#include "../arm7/source/fat/diskio.h"
#include "../arm7/source/fileService.h"
#include "../arm7/source/directoryService.h"

static FILE *disk;
static int failWrite = -1;
DSTATUS disk_initialize(BYTE drive)
{
    return drive == 1 ? 0 : STA_NOINIT;
}
DSTATUS disk_status(BYTE drive)
{
    return drive == 1 ? 0 : STA_NOINIT;
}
DRESULT disk_read(BYTE drive, BYTE *data, LBA_t sector, UINT count)
{
    if (drive != 1 || fseek(disk, sector * 512, SEEK_SET))
        return RES_ERROR;
    return fread(data, 512, count, disk) == count ? RES_OK : RES_ERROR;
}
DRESULT disk_write(BYTE drive, const BYTE *data, LBA_t sector, UINT count)
{
    if (failWrite == 0)
        return RES_ERROR;
    if (failWrite > 0)
        --failWrite;
    if (drive != 1 || fseek(disk, sector * 512, SEEK_SET))
        return RES_ERROR;
    if (fwrite(data, 512, count, disk) != count)
        return RES_ERROR;
    fflush(disk);
    return RES_OK;
}
DRESULT disk_ioctl(BYTE drive, BYTE command, void *data)
{
    (void)drive;
    (void)data;
    return command == CTRL_SYNC ? RES_OK : RES_PARERR;
}
static FRESULT save(const char *text, const char *name)
{
    fv_file_request_t request = {0};
    strcpy(request.path, name);
    request.data = (void *)text;
    request.length = strlen(text);
    request.capacity = request.length + 1;
    request.operation = FV_FILE_WRITE_STATE;
    fv_fileService(&request);
    return request.result;
}
static bool readState(const char *path, char data[256])
{
    fv_file_request_t request = {0};
    strcpy(request.path, path);
    request.data = data;
    request.capacity = 256;
    fv_fileService(&request);
    return request.result == FR_OK;
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    disk = fopen(argv[1], "r+b");
    assert(disk);
    FATFS fs;
    assert(f_mount(&fs, "sd:", 1) == FR_OK);
    assert(save("volume=75\n", "settings.ini") == FR_OK);
    char data[256];
    assert(readState("sd:/data/fastvideods/settings.ini", data) && !strcmp(data, "volume=75\n"));
    assert(save("volume=100\n", "settings.ini") == FR_OK);
    assert(readState("sd:/data/fastvideods/settings.ini.bak", data) && !strcmp(data, "volume=75\n"));
    assert(save("forbidden", "../../boot.nds") == FR_DENIED);
    // Inject an SD write failure before any replacement can be committed.
    failWrite = 0;
    assert(save("volume=0\n", "settings.ini") != FR_OK);
    failWrite = -1;
    assert(f_mount(0, "sd:", 0) == FR_OK);
    assert(f_mount(&fs, "sd:", 1) == FR_OK);
    assert(readState("sd:/data/fastvideods/settings.ini", data) && !strcmp(data, "volume=100\n"));
    assert(save("volume=90\n", "settings.ini") == FR_OK);
    assert(readState("sd:/data/fastvideods/settings.ini", data) && !strcmp(data, "volume=90\n"));
    // Use the real FAT directory scanner: page through unsorted insertions
    // without losing titles after the first 64, then page backwards.
    assert(f_mkdir("sd:/videos") == FR_OK);
    for (int i = 139; i >= 0; --i)
    {
        char path[80];
        snprintf(path, sizeof(path), "sd:/videos/Episode %d.fv", i);
        FIL video;
        assert(f_open(&video, path, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
        assert(f_close(&video) == FR_OK);
    }
    fv_dir_entry_t page[64];
    fv_listdir_req_t request = {0};
    strcpy(request.path, "sd:/videos");
    request.entries = page;
    request.maxEntries = 64;
    assert(fv_listDirectory(&request, 0) && request.count == 64 && request.total == 140 && request.more);
    assert(!strcmp(page[0].name, "Episode 0.fv") && !strcmp(page[63].name, "Episode 63.fv"));
    request.boundary = page[63];
    request.direction = 1;
    assert(fv_listDirectory(&request, 0) && request.count == 64 && request.more);
    assert(!strcmp(page[0].name, "Episode 64.fv") && !strcmp(page[63].name, "Episode 127.fv"));
    request.boundary = page[63];
    assert(fv_listDirectory(&request, 0) && request.count == 12 && !request.more);
    assert(!strcmp(page[0].name, "Episode 128.fv") && !strcmp(page[11].name, "Episode 139.fv"));
    request.boundary = page[0];
    request.direction = -1;
    assert(fv_listDirectory(&request, 0) && request.count == 64 && request.more);
    assert(!strcmp(page[0].name, "Episode 64.fv") && !strcmp(page[63].name, "Episode 127.fv"));
    request.boundary = page[0];
    assert(fv_listDirectory(&request, 0) && request.count == 64 && !request.more);
    assert(!strcmp(page[0].name, "Episode 0.fv"));
    f_mount(0, "sd:", 0);
    fclose(disk);
}
