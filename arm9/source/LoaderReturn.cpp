#include <nds.h>
#include <stdlib.h>
#include <string.h>
#include "LoaderReturn.h"
#include "Preferences.h"
#include "Storage.h"
#include "gui/OptionsMenu.h"
#include "FastVideo/fvWait.h"
namespace LoaderReturn
{
bool Run()
{
    Preferences::Save();
    // Legacy libnds 1.8 transfer-region ABI: 7 halfwords of input, padding,
    // 64-bit time_t at +16 and the return-stub pointer at +24. exit() invokes it.
    // See libnds v1.8.3 source/common/libnds_internal.h and libnds_exit.arm.c.
    DC_InvalidateRange((void *)0x02fff000, 32);
    uintptr_t address = *(volatile u32 *)0x02fff018;
    if (address >= 0x02000000 && address + sizeof(__bootstub) < 0x03000000 && !(address & 3))
    {
        __bootstub *stub = (__bootstub *)address;
        DC_InvalidateRange(stub, sizeof(*stub));
        if (stub->bootsig == BOOTSIG && stub->arm9reboot && stub->arm7reboot)
            exit(0);
    }
    const char *path = Preferences::returnPath;
    if (!isDSiMode() || strncmp(path, "sd:/", 4) || !Storage::Exists(path))
    {
        OptionsMenu::Notice(
            Preferences::Text("This launch has no return stub. On DSi with Unlaunch, set return_path in "
                              "data/fastvideods/settings.ini to your loader (sd:/boot.nds).",
                              "Ce lancement n'a pas de retour. Sur DSi avec Unlaunch, renseignez return_path "
                              "dans data/fastvideods/settings.ini."));
        return false;
    }
    // Unlaunch AutoLoadInfo wire format. No launcher binary is bundled.
    // Protocol: https://problemkaputt.de/gbatek.htm#dsiautoloadonwarmboot
    char converted[260];
    if (strlen(path) + 2 >= sizeof(converted))
        return false;
    strcpy(converted, "sdmc:");
    strcpy(converted + 5, path + 3);
    unsigned char *request = (unsigned char *)0x02000800;
    memset(request, 0, 0x400);
    memcpy(request, "AutoLoadInfo", 12);
    *(u16 *)(request + 12) = 0x3f0;
    *(u32 *)(request + 16) = 3;
    *(u16 *)(request + 20) = *(u16 *)(request + 22) = 0x7fff;
    unsigned count = 0;
    for (const unsigned char *p = (const unsigned char *)converted; *p;)
    {
        unsigned code = *p++;
        if (code >= 128)
        {
            unsigned extra = (code & 0xe0) == 0xc0 ? 1 : (code & 0xf0) == 0xe0 ? 2 : 0;
            if (!extra)
                return false;
            code &= extra == 1 ? 31 : 15;
            for (unsigned i = 0; i < extra; ++i)
            {
                if ((*p & 0xc0) != 0x80)
                    return false;
                code = (code << 6) | (*p++ & 63);
            }
            if (code >= 0xd800 && code <= 0xdfff)
                return false;
        }
        if (count >= 259)
            return false;
        ((u16 *)(request + 0x38))[count++] = code;
    }
    *(u16 *)(request + 14) = swiCRC16(0xffff, request + 16, 0x3f0);
    // Suppress any older numeric-title request before selecting an SD path.
    *(volatile u32 *)0x02000300 = 0;
    DC_FlushRange((void *)0x02000300, 32);
    DC_FlushRange(request, 0x400);
    fifoSendValue32(FIFO_USER_02, IPC_CMD_PACK(IPC_CMD_RETURN_LOADER, 0));
    // If a firmware/PMIC refuses the reset, fail visibly rather than hang.
    u32 start = fv_ticks();
    while ((u32)(fv_ticks() - start) < 32728u * 3)
        swiWaitForVBlank();
    OptionsMenu::Notice(
        Preferences::Text("The loader return was not accepted.", "Le retour au lanceur a echoue."));
    return false;
}
} // namespace LoaderReturn
