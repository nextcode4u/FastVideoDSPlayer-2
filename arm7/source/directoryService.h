#pragma once
#include <nds.h>
#include "../../common/ipc.h"
u32 fv_listDirectory(fv_listdir_req_t *request, void (*idle)(void));
