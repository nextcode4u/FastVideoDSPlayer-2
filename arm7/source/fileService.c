#include <nds.h>
#include <stdio.h>
#include <string.h>
#include "fat/ff.h"
#include "fileService.h"

static FRESULT saveState(fv_file_request_t *req)
{
    char base[64], target[96], temp[100], backup[100];
    const char *name = req->path;
    if (strcmp(name, "settings.ini") && strcmp(name, "history.ini"))
        return FR_DENIED;
    sniprintf(base, sizeof(base), "%s/data", isDSiMode() ? "sd:" : "fat:");
    FRESULT result = f_mkdir(base);
    if (result != FR_OK && result != FR_EXIST)
        return result;
    strcat(base, "/fastvideods");
    result = f_mkdir(base);
    if (result != FR_OK && result != FR_EXIST)
        return result;
    sniprintf(target, sizeof(target), "%s/%.12s", base, name);
    sniprintf(temp, sizeof(temp), "%s.tmp", target);
    sniprintf(backup, sizeof(backup), "%s.bak", target);
    FIL file;
    UINT written = 0;
    result = f_open(&file, temp, FA_CREATE_ALWAYS | FA_WRITE);
    if (result != FR_OK)
        return result;
    result = f_write(&file, req->data, req->length, &written);
    if (result == FR_OK && written != req->length)
        result = FR_DISK_ERR;
    if (result == FR_OK)
        result = f_sync(&file);
    FRESULT closed = f_close(&file);
    if (result == FR_OK)
        result = closed;
    if (result != FR_OK)
        return result;
    result = f_unlink(backup);
    if (result != FR_OK && result != FR_NO_FILE)
        return result;
    result = f_rename(target, backup);
    bool hadOriginal = result == FR_OK;
    if (!hadOriginal && result != FR_NO_FILE)
        return result;
    result = f_rename(temp, target);
    if (result != FR_OK && hadOriginal)
        f_rename(backup, target);
    return result;
}

void fv_fileService(fv_file_request_t *req)
{
    req->result = FR_INVALID_PARAMETER;
    if (!memchr(req->path, 0, sizeof(req->path)) || !req->data || req->capacity > FV_FILE_LIMIT ||
        req->length > req->capacity)
        return;
    if (req->operation == FV_FILE_WRITE_STATE)
    {
        req->result = saveState(req);
        return;
    }
    if (req->operation == FV_FILE_EXISTS)
    {
        FILINFO info;
        req->result = f_stat(req->path, &info);
        if (req->result == FR_OK && (info.fattrib & AM_DIR))
            req->result = FR_NO_FILE;
        return;
    }
    if (req->operation != FV_FILE_READ)
        return;
    FIL file;
    req->length = 0;
    req->result = f_open(&file, req->path, FA_READ);
    if (req->result != FR_OK)
        return;
    if (f_size(&file) >= req->capacity)
        req->result = FR_NOT_ENOUGH_CORE;
    else
    {
        UINT read = 0;
        req->result = f_read(&file, req->data, f_size(&file), &read);
        if (req->result == FR_OK && read != f_size(&file))
            req->result = FR_DISK_ERR;
        req->length = read;
        ((char *)req->data)[read] = 0;
    }
    FRESULT closed = f_close(&file);
    if (req->result == FR_OK)
        req->result = closed;
}
