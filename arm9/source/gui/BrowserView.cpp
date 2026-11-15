#include <nds.h>
#include <stdio.h>
#include <string.h>
#include "BrowserView.h"
#include "../Preferences.h"
#include "../MediaText.h"

static void MakeDisplayName(const char *name, char *out, size_t capacity)
{
    MediaText::DisplayText(name, strlen(name), out, capacity);
}

void BrowserView::Initialize()
{
    // The player view leaves BG0/BG1 and sprites enabled on the sub screen
    // (background art, progress bar, icons, help text). Hide everything
    // before showing the console listing, otherwise those layers stay
    // visible on top of it.
    oamClear(&oamSub, 0, 128); // disables all 128 sub sprite entries
    REG_DISPCNT_SUB = MODE_0_2D;

    consoleInit(NULL, 2, BgType_Text4bpp, BgSize_T_256x256, 2, 1, false, true);
    consoleClear();

    // only the console text BG remains visible
    REG_DISPCNT_SUB = MODE_0_2D | DISPLAY_BG2_ACTIVE;
}

void BrowserView::Render(const char* dirPath, const fv_dir_entry_t* entries, u32 count, u32 total, u32 cursor,
                         u32 topLine, bool loopEnabled, int randomMode)
{
    char disp[FV_MAX_PATH_LEN];

    consoleClear();
    iprintf("\x1b[37m");

    MakeDisplayName(dirPath, disp, sizeof(disp));
    iprintf("\x1b[0;0H\x1b[33m%.32s\x1b[37m", disp);

    for (int i = 0; i < BROWSER_VISIBLE_LINES; i++)
    {
        u32 idx = topLine + i;
        iprintf("\x1b[%d;0H", 1 + i);
        if (idx < count)
        {
            char line[31];
            MakeDisplayName(entries[idx].name, line, sizeof(line));
            char suffix = entries[idx].isDir ? '/' : ' ';
            if (idx == cursor)
                iprintf("\x1b[36;1m>%-30s%c\x1b[37m", line, suffix);
            else
                iprintf(" %-30s%c", line, suffix);
        }
    }

    iprintf("\x1b[12;0H");
    if (count == 0)
        iprintf("%s", Preferences::Text("(empty folder)", "(dossier vide)"));
    else if (total > count)
        iprintf(Preferences::Text("%lu / %lu entries; L/R: page", "%lu / %lu entrees; L/R: page"),
                (unsigned long)count, (unsigned long)total);
    else
        iprintf(Preferences::Text("%lu entries", "%lu entrees"), (unsigned long)total);

    // SUPPRESSION DE LA LIGNE 22 (ALEA / BOUCLE)
    // MODIFICATION DE LA LIGNE 23 (Aide)
    iprintf("\x1b[20;0H%s\n%s\n%s",
            Preferences::Text("A: open  B: back  L/R: page", "A: ouvrir B: retour L/R: page"),
            Preferences::Text("START: options SELECT: recent", "START: options SELECT: recents"),
            Preferences::Text(".fv videos / .m3u playlists", "Videos .fv / listes .m3u"));
}
