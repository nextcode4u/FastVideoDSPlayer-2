#include <nds.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include "Storage.h"
#include "FastVideo/fvWait.h"

static fv_file_request_t request ALIGN(32);
namespace Storage
{
const char *Drive()
{
    return isDSiMode() ? "sd:" : "fat:";
}
static bool Exchange()
{
    DC_FlushRange(&request, sizeof(request));
    DC_FlushRange(request.data, request.capacity);
    fifoSendValue32(FIFO_USER_02, IPC_CMD_PACK(IPC_CMD_FILE_SERVICE, &request));
    fv_waitReply(FIFO_USER_02);
    u32 response = fifoGetValue32(FIFO_USER_02);
    DC_InvalidateRange(&request, sizeof(request));
    DC_InvalidateRange(request.data, request.capacity);
    return (response >> IPC_CMD_CMD_SHIFT) == IPC_CMD_FILE_SERVICE && !request.result;
}
bool Exists(const char *path)
{
    static char scratch[32] ALIGN(32);
    if (!path || strlen(path) >= sizeof(request.path))
        return false;
    memset(&request, 0, sizeof(request));
    strcpy(request.path, path);
    request.data = scratch;
    request.capacity = sizeof(scratch);
    request.operation = FV_FILE_EXISTS;
    return Exchange();
}
char *ReadText(const char *path, size_t capacity, int *error)
{
    if (!path || strlen(path) >= sizeof(request.path) || capacity < 2 || capacity > FV_FILE_LIMIT)
        return 0;
    memset(&request, 0, sizeof(request));
    strcpy(request.path, path);
    request.capacity = capacity;
    request.data = memalign(32, (capacity + 31) & ~31u);
    if (!request.data)
        return 0;
    memset(request.data, 0, capacity);
    bool ok = Exchange();
    if (error)
        *error = request.result;
    if (ok)
        return (char *)request.data;
    free(request.data);
    return 0;
}
bool WriteState(const char *name, const char *text)
{
    size_t length = strlen(text);
    if (strlen(name) >= sizeof(request.path) || length >= FV_FILE_LIMIT)
        return false;
    memset(&request, 0, sizeof(request));
    strcpy(request.path, name);
    request.capacity = length + 1;
    request.length = length;
    request.operation = FV_FILE_WRITE_STATE;
    request.data = memalign(32, (request.capacity + 31) & ~31u);
    if (!request.data)
        return false;
    memcpy(request.data, text, request.capacity);
    bool ok = Exchange();
    free(request.data);
    return ok;
}
} // namespace Storage
