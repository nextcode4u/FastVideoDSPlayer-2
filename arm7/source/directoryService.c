#include <nds.h>
#include <string.h>
#include <strings.h>
#include "fat/ff.h"
#include "directoryService.h"
#include "../../common/fvNames.h"

static bool hasFvExtension(const char *name)
{
    int len = strlen(name);
    return len > 3 && strcasecmp(name + len - 3, ".fv") == 0;
}

static int compareEntry(const fv_dir_entry_t *a, const fv_dir_entry_t *b)
{
    if (a->isDir != b->isDir)
        return a->isDir ? -1 : 1;
    return fv_naturalCompare(a->name, b->name);
}
static bool hasPlaylistExtension(const char *name)
{
    const char *ext = strrchr(name, '.');
    return ext && (!strcasecmp(ext, ".m3u") || !strcasecmp(ext, ".m3u8"));
}
/* Keep a bounded, naturally ordered page while scanning. The caller owns
   the output in main RAM; no whole-directory allocation is needed. */
u32 fv_listDirectory(fv_listdir_req_t *req, void (*idle)(void))
{
    DIR dir;
    FILINFO info;
    fv_dir_entry_t candidate;
    req->count = req->total = req->more = 0;
    if (!req->maxEntries || req->maxEntries > 512 || f_opendir(&dir, req->path) != FR_OK)
        return 0;
    FRESULT result;
    unsigned eligible = 0;
    while ((result = f_readdir(&dir, &info)) == FR_OK && info.fname[0])
    {
        if (info.fname[0] == '.')
            continue;
        candidate.isDir = (info.fattrib & AM_DIR) != 0;
        if (!candidate.isDir && !hasFvExtension(info.fname) && !hasPlaylistExtension(info.fname))
            continue;
        if (strlen(req->path) + strlen(info.fname) + 2 > FV_MAX_PATH_LEN)
            continue;
        strcpy(candidate.name, info.fname);
        ++req->total;
        int boundary = compareEntry(&candidate, &req->boundary);
        if (req->direction > 0 && boundary <= 0)
            continue;
        if (req->direction < 0 && boundary >= 0)
            continue;
        ++eligible;
        unsigned lo = 0, hi = req->count;
        while (lo < hi)
        {
            unsigned mid = (lo + hi) / 2;
            int cmp = compareEntry(&candidate, &req->entries[mid]);
            if (req->direction < 0)
                cmp = -cmp;
            if (cmp < 0)
                hi = mid;
            else
                lo = mid + 1;
        }
        if (lo < req->maxEntries)
        {
            unsigned last = req->count < req->maxEntries ? req->count++ : req->maxEntries - 1;
            memmove(&req->entries[lo + 1], &req->entries[lo], (last - lo) * sizeof(candidate));
            req->entries[lo] = candidate;
        }
        if (idle)
            idle();
    }
    f_closedir(&dir);
    req->more = eligible > req->count;
    if (req->direction < 0)
        for (unsigned i = 0; i < req->count / 2; ++i)
        {
            candidate = req->entries[i];
            req->entries[i] = req->entries[req->count - i - 1];
            req->entries[req->count - i - 1] = candidate;
        }
    return result == FR_OK;
}
