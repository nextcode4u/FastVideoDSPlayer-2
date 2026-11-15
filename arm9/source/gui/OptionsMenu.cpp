#include <nds.h>
#include <stdio.h>
#include <string.h>
#include "OptionsMenu.h"
#include "BrowserView.h"
#include "PlayerController.h"
#include "../Preferences.h"
#include "../MediaText.h"
namespace OptionsMenu
{
using Preferences::Text;
static void Begin()
{
    PlayerController::RestoreSubScreen();
    lcdMainOnTop();
    BrowserView view;
    view.Initialize();
    scanKeys();
}
static unsigned Keys()
{
    swiWaitForVBlank();
    scanKeys();
    if (keysHeld() & KEY_LID)
        return 0;
    return keysDown();
}
static void Row(int y, const char *text, bool selected)
{
    char visible[31];
    MediaText::DisplayText(text, strlen(text), visible, sizeof(visible));
    iprintf("\x1b[%d;0H%s%c%-30s\x1b[37m", y, selected ? "\x1b[36;1m" : "\x1b[37m", selected ? '>' : ' ',
            visible);
}
int Choose(const char *title, const char *const *items, int count, int selected)
{
    Begin();
    if (!count)
        return -1;
    if (selected < 0 || selected >= count)
        selected = 0;
    bool dirty = true;
    for (;;)
    {
        if (dirty)
        {
            consoleClear();
            Row(0, title, false);
            int start = (selected / 9) * 9;
            for (int i = start; i < count && i < start + 9; ++i)
                Row(2 + (i - start) * 2, items[i], i == selected);
            iprintf("\x1b[21;0H%d/%d\n%s", selected + 1, count,
                    Text("A: select  B: back  L/R: page", "A: choisir B: retour L/R: page"));
            dirty = false;
        }
        unsigned keys = Keys();
        int old = selected;
        if (keys & KEY_B)
            return -1;
        if (keys & KEY_A)
            return selected;
        if (keys & KEY_UP)
            selected = selected ? selected - 1 : count - 1;
        if (keys & KEY_DOWN)
            selected = (selected + 1) % count;
        if (keys & KEY_L)
            selected = selected >= 9 ? selected - 9 : 0;
        if (keys & KEY_R)
            selected = selected + 9 < count ? selected + 9 : count - 1;
        if (keys & KEY_TOUCH)
        {
            touchPosition touch;
            touchRead(&touch);
            int row = (touch.py / 8 - 2) / 2;
            int index = (selected / 9) * 9 + row;
            if (touch.py >= 16 && touch.py < 160 && row < 9 && index < count)
                return index;
        }
        if (old != selected)
            dirty = true;
    }
}
void Notice(const char *message)
{
    Begin();
    consoleClear();
    iprintf("%.240s\n\n%s", message, Text("A / B: continue", "A / B: continuer"));
    while (!(Keys() & (KEY_A | KEY_B)))
    {
    }
}
Action Show(bool playing, const char *currentDirectory)
{
    Begin();
    int selected = 0;
    bool dirty = true, changed = false;
    const int actions = playing ? 7 : 5, total = actions + Preferences::Count;
    const char *en[] = {"Continue video", "Back to videos", "Return to launcher", "Save bookmark",
                        "Go to bookmark", "Playlist queue", "Recent videos"};
    const char *fr[] = {"Continuer video", "Liste des videos", "Retour au lanceur", "Ajouter signet",
                        "Aller au signet", "Liste de lecture", "Videos recentes"};
    const char *browserEn[] = {"Back to videos", "Recent videos", "Use this as video folder",
                               "Browse SD card", "Return to launcher"};
    const char *browserFr[] = {"Liste des videos", "Videos recentes", "Utiliser ce dossier video",
                               "Parcourir la carte SD", "Retour au lanceur"};
    for (;;)
    {
        if (dirty)
        {
            consoleClear();
            Row(0, Text("Playback options", "Options de lecture"), false);
            int start = (selected / 8) * 8;
            for (int i = start; i < total && i < start + 8; ++i)
            {
                char row[80];
                if (i < actions)
                    snprintf(
                        row, sizeof(row), "%s",
                        playing ? (Preferences::values[Preferences::Language] ? fr[i] : en[i])
                                : (Preferences::values[Preferences::Language] ? browserFr[i] : browserEn[i]));
                else
                {
                    int key = i - actions;
                    char value[24];
                    Preferences::ValueText((Preferences::Key)key, value, sizeof(value));
                    snprintf(row, sizeof(row), "%s: %s",
                             Text(Preferences::options[key].english, Preferences::options[key].french),
                             value);
                }
                Row(2 + (i - start) * 2, row, i == selected);
            }
            iprintf("\x1b[19;0H%s\n%s\n%s",
                    Text("Left/Right: change  L/R: page", "Gauche/Droite: choix L/R: page"),
                    Text("A: choose  B: save and back", "A: choisir B: sauver, retour"),
                    changed ? Text("Unsaved changes", "Modifications non enregistrees") : "");
            dirty = false;
        }
        unsigned keys = Keys();
        int old = selected;
        Action action = Continue;
        bool done = false;
        if (keys & KEY_TOUCH)
        {
            touchPosition touch;
            touchRead(&touch);
            if (touch.py >= 16 && touch.py < 144)
            {
                int row = (selected / 8) * 8 + (touch.py / 8 - 2) / 2;
                if (row < total)
                {
                    if (row == selected)
                        keys |= KEY_A;
                    else
                        selected = row;
                }
            }
        }
        if (keys & KEY_B)
        {
            done = true;
            action = Continue;
        }
        if (keys & KEY_UP)
            selected = selected ? selected - 1 : total - 1;
        if (keys & KEY_DOWN)
            selected = (selected + 1) % total;
        if (keys & KEY_L)
            selected = selected >= 8 ? selected - 8 : 0;
        if (keys & KEY_R)
            selected = selected + 8 < total ? selected + 8 : total - 1;
        if (selected >= actions && (keys & (KEY_LEFT | KEY_RIGHT | KEY_A)))
        {
            Preferences::Change((Preferences::Key)(selected - actions), (keys & KEY_LEFT) ? -1 : 1);
            dirty = changed = true;
        }
        else if (selected < actions && (keys & KEY_A))
        {
            done = true;
            if (playing)
                action = (Action)selected;
            else
            {
                const Action map[] = {Back, Recent, SetRoot, BrowseRoot, Return};
                action = map[selected];
            }
            if (action == SetRoot && currentDirectory)
            {
                snprintf(Preferences::videoRoot, sizeof(Preferences::videoRoot), "%s", currentDirectory);
                changed = true;
            }
        }
        if (old != selected)
            dirty = true;
        if (done)
        {
            if (changed && !Preferences::Save())
                Notice(Text("Could not save settings. Check SD space / write protection.",
                            "Echec sauvegarde. Verifiez la carte SD."));
            return action;
        }
    }
}
} // namespace OptionsMenu
