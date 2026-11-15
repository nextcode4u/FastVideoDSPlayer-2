#include <nds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <initializer_list>
#include "PlayerView.h"
#include "UiCanvas.h"
#include "../Preferences.h"
#include "../MediaText.h"
extern u32 GetDebounceTicks();

PlayerView::PlayerView() {}
void PlayerView::Initialize()
{
    UiCanvas::Get().Initialize();
    _headerDirty = _captionsDirty = _timelineDirty = _transportDirty = true;
    _lastVolume = _lastMute = _lastScale = _lastRepeat = -1;
    _titleEpoch = GetDebounceTicks();
    Update();
    VBlank();
}
static void TimeText(u32 seconds, char *out, size_t size)
{
    if (seconds >= 3600)
        snprintf(out, size, "%lu:%02lu:%02lu", (unsigned long)(seconds / 3600),
                 (unsigned long)(seconds / 60 % 60), (unsigned long)(seconds % 60));
    else
        snprintf(out, size, "%lu:%02lu", (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
}
void PlayerView::DrawHeader()
{
    using namespace Preferences;
    auto &ui = UiCanvas::Get();
    ui.Fill(0, 0, 256, 49, UiCanvas::Background);
    ui.Circle(24, 24, 16, _pressed == PlayerControls::Back ? UiCanvas::Pressed : UiCanvas::Surface);
    ui.Fill(18, 19, 13, 2, UiCanvas::Text);
    for (int i = 0; i < 5; ++i)
    {
        ui.Pixel(18 + i, 19 - i, UiCanvas::Text);
        ui.Pixel(18 + i, 20 + i, UiCanvas::Text);
    }
    ui.Centered(24, 27, "B", UiCanvas::Muted);
    int titleWidth = ui.Width(_title, UiCanvas::Heading);
    ui.TextAt(48 - _titleOffset, 7, _title, UiCanvas::Bright, 48, 240, UiCanvas::Heading);
    if (titleWidth > 192)
        ui.TextAt(48 - _titleOffset + titleWidth + 24, 7, _title, UiCanvas::Bright, 48, 240, UiCanvas::Heading);
    if (_noticeVisible)
        ui.TextAt(48, 31, _notice, UiCanvas::Accent, 48, 240);
    else
    {
        char state[48], volume[24];
        snprintf(state, sizeof(state), "%s / %s", _playing ? Text("Playing", "Lecture") : Text("Paused", "Pause"),
                 values[Scale] ? Text("Stretch", "Etirer") : Text("Fit", "Adapter"));
        snprintf(volume, sizeof(volume), "%s", Text("Muted", "Muet"));
        if (!values[Mute])
            snprintf(volume, sizeof(volume), "Vol %d%%", (values[Volume] * 100 + 63) / 127);
        int right = 240 - ui.Width(volume);
        ui.TextAt(48, 31, state, UiCanvas::Muted, 48, right - 8);
        ui.TextAt(right, 31, volume, UiCanvas::Muted);
    }
    ui.Fill(16, 48, 224, 1, UiCanvas::Line);
}
void PlayerView::DrawCaptions()
{
    auto &ui = UiCanvas::Get();
    ui.Fill(0, 50, 256, 53, UiCanvas::Background);
    char lines[3][129] = {};
    int count = 0, maximum = *_diagnostics ? 2 : 3;
    const char *text = _caption;
    while (*text && count < maximum)
    {
        size_t take = 0, space = 0;
        while (text[take] && take < 128)
        {
            lines[count][take] = text[take];
            lines[count][take + 1] = 0;
            if (ui.Width(lines[count]) > 224) break;
            if (text[take] == ' ') space = take;
            ++take;
        }
        if (text[take] && space) take = space;
        if (!take) take = 1;
        memcpy(lines[count], text, take);
        lines[count++][take] = 0;
        text += take;
        while (*text == ' ') ++text;
    }
    int y = 55 + (maximum * 13 - count * 13) / 2;
    for (int row = 0; row < count; ++row)
        ui.Centered(128, y + row * 13, lines[row], UiCanvas::Text);
    if (*_diagnostics)
        ui.TextAt(16, 88, _diagnostics, UiCanvas::Muted, 16, 240);
}
void PlayerView::DrawTimeline()
{
    auto &ui = UiCanvas::Get();
    ui.Fill(0, 103, 256, 33, UiCanvas::Background);
    char current[24], total[24];
    TimeText(_curTime, current, sizeof(current));
    TimeText(_totalTime, total, sizeof(total));
    ui.TextAt(16, 105, current, UiCanvas::Text);
    ui.TextAt(240 - ui.Width(total), 105, total, UiCanvas::Muted);
    ui.Fill(16, 124, 224, 3, UiCanvas::Line);
    int width = _totalTime ? (u64)_curTime * 224 / _totalTime : 0;
    if (width > 224) width = 224;
    ui.Fill(16, 124, width, 3, UiCanvas::Accent);
    ui.Circle(16 + width, 125, 3, UiCanvas::Accent);
}
void PlayerView::DrawTransport()
{
    using namespace PlayerControls;
    auto &ui = UiCanvas::Get();
    ui.Fill(0, 136, 256, 56, UiCanvas::Background);
    for (int b = Repeat; b <= Options; ++b)
    {
        int x = Buttons[b].x + Buttons[b].width / 2;
        if (_pressed == b && b != PlayPause)
            ui.Circle(x, 155, 17, UiCanvas::Pressed);
    }
    auto repeat = Preferences::values[Preferences::Repeat] ? UiCanvas::Accent : UiCanvas::Muted;
    ui.Fill(20, 150, 16, 2, repeat);
    ui.Fill(20, 160, 16, 2, repeat);
    for (int i = 0; i < 5; ++i)
    {
        ui.Pixel(35 - i, 150 - i, repeat); ui.Pixel(35 - i, 151 + i, repeat);
        ui.Pixel(20 + i, 160 - i, repeat); ui.Pixel(20 + i, 161 + i, repeat);
    }
    ui.Fill(66, 148, 2, 15, UiCanvas::Text);
    ui.Fill(188, 148, 2, 15, UiCanvas::Text);
    for (int row = -7; row <= 7; ++row)
    {
        ui.Fill(69 + abs(row), 155 + row, 9 - abs(row), 1, UiCanvas::Text);
        ui.Fill(178, 155 + row, 9 - abs(row), 1, UiCanvas::Text);
    }
    ui.Circle(128, 155, 18, _pressed == PlayPause ? UiCanvas::AccentLight : UiCanvas::Accent);
    if (_playing)
    {
        ui.Fill(122, 148, 4, 15, UiCanvas::Background);
        ui.Fill(130, 148, 4, 15, UiCanvas::Background);
    }
    else
        for (int row = -7; row <= 7; ++row)
            ui.Fill(125, 155 + row, 10 - abs(row), 1, UiCanvas::Background);
    for (int x : {220, 228, 236}) ui.Circle(x, 155, 2, UiCanvas::Text);
    ui.Centered(28, 177, Preferences::Text("Repeat", "Boucle"), repeat);
    ui.Centered(76, 177, Preferences::Text("L Prev", "L Prec"), UiCanvas::Muted);
    ui.Centered(128, 177, "A", UiCanvas::Muted);
    ui.Centered(180, 177, Preferences::Text("R Next", "R Suiv"), UiCanvas::Muted);
    ui.Centered(228, 177, "Options", UiCanvas::Muted);
}
void PlayerView::Update()
{
    using namespace Preferences;
    u32 now = GetDebounceTicks();
    if (_noticeVisible && (int32_t)(now - _noticeUntil) >= 0)
    {
        _noticeVisible = false;
        _headerDirty = true;
    }
    int width = UiCanvas::Get().Width(_title, UiCanvas::Heading);
    int offset = width > 192 ? ((now - _titleEpoch) / 1636) % (width + 24) : 0;
    if (_titleOffset != offset) { _titleOffset = offset; _headerDirty = true; }
    if (_lastVolume != values[Volume] || _lastMute != values[Mute] || _lastScale != values[Scale])
    {
        _lastVolume = values[Volume]; _lastMute = values[Mute]; _lastScale = values[Scale];
        _headerDirty = true;
    }
    if (_lastRepeat != values[Repeat]) { _lastRepeat = values[Repeat]; _transportDirty = true; }
    if (_headerDirty) DrawHeader();
    if (_captionsDirty) DrawCaptions();
    if (_timelineDirty) DrawTimeline();
    if (_transportDirty) DrawTransport();
    _headerDirty = _captionsDirty = _timelineDirty = _transportDirty = false;
}
void PlayerView::VBlank() { UiCanvas::Get().Present(); }
void PlayerView::SetTotalTime(u32 time) { _totalTime = time; _timelineDirty = true; }
void PlayerView::SetCurrentTime(u32 time)
{
    if (_curTime != time) { _curTime = time; _timelineDirty = true; }
}
void PlayerView::SetPlaying(bool playing)
{
    if (_playing != playing) { _playing = playing; _headerDirty = _transportDirty = true; }
}
void PlayerView::SetPressed(PlayerControls::Button button)
{
    if (_pressed != button)
    {
        _pressed = button;
        _headerDirty = _transportDirty = true;
        Update();
        VBlank();
    }
}
void PlayerView::SetDetails(const char *title, const char *caption, const char *diagnostics)
{
    char display[256];
    MediaText::DisplayText(title ? title : "", title ? strlen(title) : 0, display, sizeof(display));
    size_t length = strlen(display);
    if (length > 3 && !strcasecmp(display + length - 3, ".fv")) display[length - 3] = 0;
    if (strcmp(_title, display))
    {
        strcpy(_title, display); _titleEpoch = GetDebounceTicks(); _headerDirty = true;
    }
    if (!caption) caption = "";
    if (!diagnostics) diagnostics = "";
    if (strcmp(_caption, caption) || strcmp(_diagnostics, diagnostics))
    {
        snprintf(_caption, sizeof(_caption), "%s", caption);
        snprintf(_diagnostics, sizeof(_diagnostics), "%s", diagnostics);
        _captionsDirty = true;
    }
    Update();
    VBlank();
}
void PlayerView::SetMessage(const char *first, const char *second)
{
    char message[192];
    snprintf(message, sizeof(message), "%s%s%s", first ? first : "", first && *first && second && *second ? " / " : "",
             second ? second : "");
    MediaText::DisplayText(message, strlen(message), _notice, sizeof(_notice));
    _noticeVisible = true;
    _noticeUntil = GetDebounceTicks() + 32728u * 3;
    _headerDirty = true;
}
