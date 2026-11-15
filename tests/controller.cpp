#include <nds.h>
#include <cassert>
#include "../arm9/source/Preferences.h"
#include "../arm9/source/Storage.h"
#include "../arm9/source/gui/PlayerController.h"

namespace Storage
{
const char *Drive()
{
    return "sd:";
}
char *ReadText(const char *, size_t, int *)
{
    return nullptr;
}
bool WriteState(const char *, const char *)
{
    return true;
}
} // namespace Storage

u16 testKeys, REG_MASTER_BRIGHT_SUB, REG_DIVCNT;
touchPosition testTouch = { 120, 150 };
u64 REG_DIV_NUMER;
u32 REG_DIV_DENOM_L, REG_DIV_RESULT_L;
static int pauses, resumes, seeks, scaleChanges;
static fv_scale_mode_t scaleMode = FV_SCALE_FIT;
extern bool gScreenSwapped;

u32 GetDebounceTicks() { static u32 ticks; return ticks += 10000; }
NtftFont::NtftFont(const void*) : _font{} {}
PlayerView::PlayerView() : _robotoRegular10(nullptr) {}
void PlayerView::Initialize() {}
void PlayerView::Update() {}
void PlayerView::VBlank() {}
void PlayerView::SetTotalTime(u32) {}
void PlayerView::SetCurrentTime(u32) {}
void PlayerView::SetMessage(const char*, const char*) {}
void fv_pausePlayer(fv_player_t* p) { ++pauses; p->isPlaying = false; }
void fv_resumePlayer(fv_player_t* p) { ++resumes; p->isPlaying = true; }
void fv_updatePlayer(fv_player_t*) {}
void fv_startPlayer(fv_player_t* p) { p->isPlaying = true; }
void fv_gotoKeyFrame(fv_player_t*, u32) { ++seeks; }
void fv_gotoNearestKeyFrame(fv_player_t*, u32) { ++seeks; }
fv_scale_mode_t fv_getScaleMode() { return scaleMode; }
void fv_setScaleMode(fv_player_t*, fv_scale_mode_t mode) { ++scaleChanges; scaleMode = mode; }

static PlayerController::NavAction input(PlayerController& controller, u16 keys)
{
    testKeys = keys;
    return controller.Update();
}

int main()
{
    Preferences::Defaults("sd:");
    fv_header_t header{};
    header.width = 256; header.height = 144;
    header.fpsNum = 30; header.fpsDen = 1;
    header.nrFrames = 3000; header.nrKeyFrames = 10;
    fv_player_t player{};
    player.fvHeader = &header; player.isPlaying = true;
    PlayerController controller(&player);
    controller.Initialize();
    using C = PlayerController;

    // Scaling works without pausing and does not trigger repeat mode.
    assert(input(controller, KEY_DOWN | KEY_START) == C::NAV_ACTION_NONE);
    assert(scaleMode == FV_SCALE_STRETCH && scaleChanges == 1);
    assert(player.isPlaying && pauses == 0);
    input(controller, 0);
    input(controller, KEY_A);
    assert(!player.isPlaying && pauses == 1);
    input(controller, 0);
    assert(input(controller, KEY_DOWN | KEY_START) == C::NAV_ACTION_NONE);
    assert(scaleMode == FV_SCALE_FIT && scaleChanges == 2);
    assert(!player.isPlaying && resumes == 0);
    input(controller, 0);
    input(controller, KEY_UP | KEY_START);
    assert(gScreenSwapped && scaleChanges == 2);
    input(controller, 0);
    input(controller, KEY_UP | KEY_START);
    assert(!gScreenSwapped);
    input(controller, 0);
    input(controller, KEY_A);
    assert(player.isPlaying && resumes == 1);
    input(controller, 0);

    // A pending touch release and simultaneous navigation cannot defeat lid pause.
    input(controller, KEY_TOUCH);
    assert(input(controller, KEY_LID | KEY_A | KEY_R | KEY_START | KEY_DOWN) == C::NAV_ACTION_NONE);
    assert(!player.isPlaying && pauses == 2 && resumes == 1 && scaleChanges == 2);
    for (int i = 0; i < 30; ++i)
        assert(input(controller, KEY_LID | KEY_LEFT | KEY_TOUCH) == C::NAV_ACTION_NONE);
    assert(seeks == 0 && pauses == 2);

    // Reopening while holding A/next must not resume or change videos.
    assert(input(controller, KEY_A | KEY_R) == C::NAV_ACTION_NONE);
    assert(!player.isPlaying && resumes == 1);
    assert(input(controller, KEY_A | KEY_R) == C::NAV_ACTION_NONE);
    assert(!player.isPlaying && resumes == 1);
    input(controller, 0);
    input(controller, KEY_A);
    assert(player.isPlaying && resumes == 2);

    // Already-paused playback remains paused across another lid cycle.
    input(controller, 0);
    input(controller, KEY_A);
    assert(!player.isPlaying && pauses == 3);
    input(controller, KEY_LID);
    input(controller, 0);
    assert(!player.isPlaying && pauses == 3 && resumes == 2);

    // Seeking while paused stays paused; dragging previews before committing.
    int before = seeks;
    controller.SeekFrame(900);
    assert(seeks > before && !player.isPlaying);
    input(controller, 0);
    testTouch = {120, 118};
    before = seeks;
    input(controller, KEY_TOUCH);
    assert(seeks == before);
    input(controller, 0);
    assert(seeks > before && !player.isPlaying);
    // START opens on press without toggling repeat; holding/releasing is inert.
    assert(input(controller, KEY_START) == C::NAV_ACTION_OPTIONS);
    assert(input(controller, KEY_START) == C::NAV_ACTION_NONE);
    assert(input(controller, 0) == C::NAV_ACTION_NONE);
    assert(input(controller, KEY_SELECT) == C::NAV_ACTION_TOGGLE_RANDOM);
    assert(input(controller, 0) == C::NAV_ACTION_NONE);
    assert(input(controller, KEY_START | KEY_SELECT) == C::NAV_ACTION_OPTIONS);
    assert(input(controller, 0) == C::NAV_ACTION_NONE);

    // Touch buttons activate on release, and dragging away cancels the tap.
    testTouch = {200, 155};
    assert(input(controller, KEY_TOUCH) == C::NAV_ACTION_NONE);
    assert(input(controller, 0) == C::NAV_ACTION_OPTIONS);
    testTouch = {50, 155};
    assert(input(controller, KEY_TOUCH) == C::NAV_ACTION_NONE);
    assert(input(controller, 0) == C::NAV_ACTION_TOGGLE_LOOP);
    testTouch = {200, 155};
    input(controller, KEY_TOUCH);
    testTouch = {150, 155};
    input(controller, KEY_TOUCH);
    assert(input(controller, 0) == C::NAV_ACTION_NONE);

    // Video-on-touch-screen has no invisible buttons. Lid closure cancels taps.
    gScreenSwapped = true;
    testTouch = {200, 155};
    assert(input(controller, KEY_TOUCH) == C::NAV_ACTION_SHOW_INFO);
    assert(input(controller, 0) == C::NAV_ACTION_NONE);
    gScreenSwapped = false;
    input(controller, KEY_TOUCH);
    assert(input(controller, KEY_LID) == C::NAV_ACTION_NONE);
    assert(input(controller, 0) == C::NAV_ACTION_NONE);

    // Closing at EOF suppresses automatic next/repeat until an explicit resume.
    input(controller, KEY_A);
    player.videoEnded = true;
    assert(input(controller, KEY_LID) == C::NAV_ACTION_NONE);
    assert(input(controller, 0) == C::NAV_ACTION_NONE);
    assert(!player.isPlaying);
    input(controller, KEY_A);
    assert(input(controller, 0) == C::NAV_ACTION_VIDEO_ENDED);
    return 0;
}
