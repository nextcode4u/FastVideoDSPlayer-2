#include <nds.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include "OptionsMenu.h"
#include "UiCanvas.h"
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
    UiCanvas::Get().Initialize();
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
    auto &ui = UiCanvas::Get();
    ui.Clear();
    ui.TextAt(16, 9, title, UiCanvas::Bright, 16, 216, UiCanvas::Heading);
    char number[32];
    snprintf(number, sizeof(number), "%d / %d", page + 1, pages);
    ui.TextAt(240 - ui.Width(number), 13, number, UiCanvas::Muted);
    ui.Fill(16, 46, 224, 1, UiCanvas::Line);
    const int centres[] = {44, 132, 212};
    const char *labels[] = {Text("B Back", "B Retour"), Text("L Prev", "L Prec"), Text("R Next", "R Suiv")};
    for (int i = 0; i < 3; ++i)
    {
        ui.Fill(centres[i] - 36, 172, 72, 20, UiCanvas::Surface);
        ui.Centered(centres[i], 175, labels[i], UiCanvas::Text);
    }
}
static void Row(int index, const char *text, bool selected, const char *value = nullptr)
{
    auto &ui = UiCanvas::Get();
    char visible[256];
    MediaText::DisplayText(text, strlen(text), visible, sizeof(visible));
    int y = 52 + index * 14;
    if (selected)
    {
        ui.Fill(8, y - 1, 240, 14, UiCanvas::Surface);
        ui.Fill(8, y + 1, 2, 10, UiCanvas::Accent);
    }
    int right = value ? 240 - ui.Width(value) : 248;
    ui.TextAt(16, y, visible, selected ? UiCanvas::Bright : UiCanvas::Text, 16, right - 8);
    if (value) ui.TextAt(right, y, value, selected ? UiCanvas::Accent : UiCanvas::Muted);
}
static void Hint(const char *text)
{
    UiCanvas::Get().TextAt(16, 31, text, UiCanvas::Muted, 16, 240);
}
static int TouchRow(const touchPosition &touch)
{
    if (touch.px < 8 || touch.px >= 248 || touch.py < 51 || touch.py >= 163)
        return -1;
    return (touch.py - 51) / 14;
}
static unsigned FooterTouch(const touchPosition &touch)
{
    if (touch.py < 172 || touch.py >= 192)
        return 0;
    if (touch.px >= 8 && touch.px < 80)
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
            char hint[64];
            snprintf(hint, sizeof(hint), "%d / %d   %s", selected + 1, count, Text("A: choose", "A: choisir"));
            Hint(hint);
            UiCanvas::Get().Present();
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
    auto &ui = UiCanvas::Get();
    ui.Clear();
    ui.TextAt(16, 9, Text("Player", "Lecteur"), UiCanvas::Bright, 16, 240, UiCanvas::Heading);
    char visible[256];
    MediaText::DisplayText(message, strlen(message), visible, sizeof(visible));
    size_t used = 0, length = strlen(visible);
    for (int y = 45; used < length && y < 150; y += 15)
    {
        char line[256] = {};
        size_t take = 0, space = 0;
        while (used + take < length)
        {
            line[take] = visible[used + take]; line[take + 1] = 0;
            if (ui.Width(line) > 224) break;
            if (line[take] == ' ') space = take;
            ++take;
        }
        if (used + take < length && space) take = space;
        if (!take) take = 1;
        line[take] = 0;
        ui.TextAt(16, y, line);
        used += take;
        while (visible[used] == ' ') ++used;
    }
    ui.TextAt(16, 174, Text("A / B: continue", "A / B: continuer"), UiCanvas::Muted);
    ui.Present();
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
                if (entries[i].setting)
                {
                    auto key = (Preferences::Key)entries[i].value;
                    char value[24];
                    Preferences::ValueText(key, value, sizeof(value));
                    Row(i, Text(Preferences::options[key].english, Preferences::options[key].french),
                        i == selected, value);
                }
                else
                    Row(i, ActionText((Action)entries[i].value), i == selected);
            }
            char hint[64];
            snprintf(hint, sizeof(hint), "%s%s", entries[selected].setting
                        ? Text("Left / Right: change", "Gauche / Droite: changer") : Text("A: open", "A: ouvrir"),
                    changed ? Text("   Edited", "   Modifie") : "");
            Hint(hint);
            UiCanvas::Get().Present();
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
