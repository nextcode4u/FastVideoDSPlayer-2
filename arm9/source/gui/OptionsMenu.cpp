#include <nds.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include "OptionsMenu.h"
#include "BrowserView.h"
#include "ControlSurface.h"
#include "PlayerController.h"
#include "../Preferences.h"
#include "../MediaText.h"

namespace OptionsMenu
{
using Preferences::Text;
static const int PageCount = 4;
static const int RowsPerPage = 8;

static void Begin()
{
    PlayerController::RestoreSubScreen();
    lcdMainOnTop();
    BrowserView view;
    view.Initialize();
    ControlSurface::Initialize();
    scanKeys(); // Consume the button/touch that opened the panel.
}
static unsigned Keys()
{
    swiWaitForVBlank();
    scanKeys();
    if (keysHeld() & KEY_LID)
        return 0;
    return keysDown();
}
static void Frame(const char *title, int page, int pages)
{
    consoleClear();
    ControlSurface::Fill(0, 0, 32, 24, ControlSurface::Header);
    ControlSurface::Fill(1, 4, 30, 16, ControlSurface::Panel);
    ControlSurface::Fill(1, 21, 10, 3, ControlSurface::Button);
    ControlSurface::Fill(12, 21, 9, 3, ControlSurface::Button);
    ControlSurface::Fill(22, 21, 9, 3, ControlSurface::Button);
    iprintf("\x1b[37m\x1b[1;2H%.23s\x1b[1;26H%d/%d", title, page + 1, pages);
    iprintf("\x1b[22;2H%s\x1b[22;13H%s\x1b[22;23H%s",
            Text("B: Back", "B: Retour"), Text("< L Prev", "< L Prec"), Text("R Next >", "R Suiv >"));
}
static void Row(int index, const char *text, bool selected)
{
    char visible[29];
    MediaText::DisplayText(text, strlen(text), visible, sizeof(visible));
    int y = 4 + index * 2;
    if (selected)
        ControlSurface::Fill(1, y, 30, 2, ControlSurface::Selected);
    iprintf("\x1b[%d;1H%c%-28s", y, selected ? '>' : ' ', visible);
}
static int TouchRow(const touchPosition &touch)
{
    if (touch.px < 8 || touch.px >= 248 || touch.py < 32 || touch.py >= 160)
        return -1;
    return (touch.py - 32) / 16;
}
static unsigned FooterTouch(const touchPosition &touch)
{
    if (touch.py < 168 || touch.py >= 192)
        return 0;
    if (touch.px >= 8 && touch.px < 88)
        return KEY_B;
    if (touch.px >= 96 && touch.px < 168)
        return KEY_L;
    if (touch.px >= 176 && touch.px < 248)
        return KEY_R;
    return 0;
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
            Frame(title, selected / RowsPerPage, (count + RowsPerPage - 1) / RowsPerPage);
            int start = (selected / RowsPerPage) * RowsPerPage;
            for (int i = start; i < count && i < start + RowsPerPage; ++i)
                Row(i - start, items[i], i == selected);
            iprintf("\x1b[3;2H%d/%d  %s", selected + 1, count,
                    Text("A: choose", "A: choisir"));
            dirty = false;
        }
        unsigned keys = Keys();
        int old = selected;
        if (keys & KEY_TOUCH)
        {
            touchPosition touch;
            touchRead(&touch);
            keys |= FooterTouch(touch);
            int row = TouchRow(touch);
            int index = (selected / RowsPerPage) * RowsPerPage + row;
            if (row >= 0 && index < count)
                return index;
        }
        if (keys & (KEY_B | KEY_START))
            return -1;
        if (keys & KEY_A)
            return selected;
        if (keys & KEY_UP)
            selected = selected ? selected - 1 : count - 1;
        if (keys & KEY_DOWN)
            selected = (selected + 1) % count;
        if (keys & KEY_L)
            selected = selected >= RowsPerPage ? selected - RowsPerPage : 0;
        if (keys & KEY_R)
            selected = selected + RowsPerPage < count ? selected + RowsPerPage : count - 1;
        if (old != selected)
            dirty = true;
    }
}
void Notice(const char *message)
{
    Begin();
    ControlSurface::Fill(0, 0, 32, 24, ControlSurface::Header);
    iprintf("%.240s\n\n%s", message, Text("A / B: continue", "A / B: continuer"));
    while (!(Keys() & (KEY_A | KEY_B | KEY_START)))
    {
    }
}

struct Entry
{
    bool setting;
    int value;
};
static int Entries(int page, bool playing, Entry *out)
{
    using namespace Preferences;
    int count = 0;
    auto action = [&](Action value) { out[count++] = {false, value}; };
    auto setting = [&](Key value) { out[count++] = {true, value}; };
    if (page == 0)
    {
        if (playing) action(Continue);
        action(Back);
        action(Return);
        for (Key key : {Repeat, Shuffle, Seek, EndAction, Resume}) setting(key);
    }
    else if (page == 1)
        for (Key key : {Scale, Screen, Dim, Subtitles, SubtitleDelay, PerVideo}) setting(key);
    else if (page == 2)
        for (Key key : {Volume, Mute, Mono, AudioDelay}) setting(key);
    else
    {
        if (playing)
        {
            action(Bookmark);
            action(GoBookmark);
            action(Queue);
        }
        action(Recent);
        if (!playing)
        {
            action(SetRoot);
            action(BrowseRoot);
        }
        setting(Language);
        setting(Diagnostics);
    }
    return count;
}
static const char *ActionText(Action action)
{
    const char *en[] = {"Continue video", "Back to videos", "Return to launcher", "Save bookmark",
                       "Go to bookmark", "Playlist queue", "Recent videos", "Use this as video folder",
                       "Browse SD card"};
    const char *fr[] = {"Continuer video", "Liste des videos", "Retour au lanceur", "Ajouter signet",
                       "Aller au signet", "Liste de lecture", "Videos recentes", "Utiliser ce dossier video",
                       "Parcourir la carte SD"};
    return Text(en[action], fr[action]);
}
Action Show(bool playing, const char *currentDirectory)
{
    Begin();
    int page = 0, selected = 0;
    bool dirty = true, changed = false;
    for (;;)
    {
        Entry entries[RowsPerPage];
        int count = Entries(page, playing, entries);
        if (dirty)
        {
            const char *en[] = {"Playback", "Picture", "Sound", "Library"};
            const char *fr[] = {"Lecture", "Image", "Son", "Bibliotheque"};
            Frame(Text(en[page], fr[page]), page, PageCount);
            for (int i = 0; i < count; ++i)
            {
                char row[80];
                if (entries[i].setting)
                {
                    auto key = (Preferences::Key)entries[i].value;
                    char value[24];
                    Preferences::ValueText(key, value, sizeof(value));
                    snprintf(row, sizeof(row), "%s: %s",
                             Text(Preferences::options[key].english, Preferences::options[key].french), value);
                }
                else
                    snprintf(row, sizeof(row), "%s", ActionText((Action)entries[i].value));
                Row(i, row, i == selected);
            }
            iprintf("\x1b[3;2H%s%s", entries[selected].setting
                        ? Text("< > / A: change", "< > / A: changer") : Text("A: open", "A: ouvrir"),
                    changed ? Text("  [edited]", "  [modifie]") : "");
            dirty = false;
        }
        unsigned keys = Keys();
        if (keys & KEY_TOUCH)
        {
            touchPosition touch;
            touchRead(&touch);
            keys |= FooterTouch(touch);
            int row = TouchRow(touch);
            if (row >= 0 && row < count)
            {
                if (row == selected)
                    keys |= KEY_A;
                else
                {
                    selected = row;
                    dirty = true;
                }
            }
        }
        Action result = Continue;
        bool done = keys & (KEY_B | KEY_START);
        if (!done && (keys & (KEY_L | KEY_R)))
        {
            page = (page + ((keys & KEY_L) ? PageCount - 1 : 1)) % PageCount;
            selected = 0;
            dirty = true;
            continue;
        }
        if (!done && (keys & (KEY_UP | KEY_DOWN)))
        {
            selected = (selected + ((keys & KEY_UP) ? count - 1 : 1)) % count;
            dirty = true;
        }
        if (!done && entries[selected].setting && (keys & (KEY_LEFT | KEY_RIGHT | KEY_A)))
        {
            Preferences::Change((Preferences::Key)entries[selected].value, (keys & KEY_LEFT) ? -1 : 1);
            dirty = changed = true;
        }
        else if (!done && !entries[selected].setting && (keys & KEY_A))
        {
            result = (Action)entries[selected].value;
            done = true;
            if (result == SetRoot && currentDirectory)
            {
                snprintf(Preferences::videoRoot, sizeof(Preferences::videoRoot), "%s", currentDirectory);
                changed = true;
            }
        }
        if (done)
        {
            if (changed && !Preferences::Save())
                Notice(Text("Could not save settings. Check SD space / write protection.",
                            "Echec sauvegarde. Verifiez la carte SD."));
            return result;
        }
    }
}
} // namespace OptionsMenu
