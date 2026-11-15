#pragma once
#include <nds.h>
#include "PlayerControls.h"

class PlayerView
{
public:
    PlayerView();
    void Initialize();
    void Update();
    void VBlank();
    void SetTotalTime(u32 totalTime);
    void SetCurrentTime(u32 currentTime);
    void SetPlaying(bool playing);
    void SetPressed(PlayerControls::Button button);
    void SetDetails(const char *title, const char *caption, const char *diagnostics);
    void SetMessage(const char *line1, const char *line2);
private:
    void DrawHeader();
    void DrawCaptions();
    void DrawTimeline();
    void DrawTransport();
    bool _playing = false;
    bool _headerDirty = true, _captionsDirty = true, _timelineDirty = true, _transportDirty = true;
    bool _noticeVisible = false;
    u32 _curTime = 0, _totalTime = 0, _titleEpoch = 0, _noticeUntil = 0;
    int _titleOffset = 0, _lastVolume = -1, _lastMute = -1, _lastScale = -1, _lastRepeat = -1;
    PlayerControls::Button _pressed = PlayerControls::None;
    char _title[256] = {}, _caption[129] = {}, _diagnostics[64] = {}, _notice[96] = {};
};
