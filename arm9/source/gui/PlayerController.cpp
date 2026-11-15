#include <nds.h>
#include "PlayerController.h"
#include "PlayerControls.h"
#include "../Preferences.h"
#include <stdio.h>

#define DIM_WAIT_SEC 5
#define DIM_FADE_SEC 3

// Ticks at BUS_CLOCK/1024 (~32728.5 Hz on NDS), ~30.5us/tick.
// ~3273 ticks = ~100ms, comfortably longer than any real switch bounce.
#define NAV_DEBOUNCE_TICKS 3273

extern u32 GetDebounceTicks();

bool PlayerController::sSubScreenOff = false;

// Variable globale pour mémoriser l'inversion des écrans
bool gScreenSwapped = false;

PlayerController::PlayerController(fv_player_t* player)
    : _inputRepeater(KEY_LEFT | KEY_RIGHT, 12, 3), _player(player), _subScreenState(SUB_SCREEN_STATE_ACTIVE),
      _subScreenStateCounter(0), _subBacklightOff(false), _playing(true), _lastTime(-1), _seekPenDown(false),
      _seekLastFrame(-1), _seekKeyFrame(0), _pendingNavAction(NAV_ACTION_NONE),
      _videoEnded(false)
{
    for (int i = 0; i < 5; i++)
        _lastNavActionVBlank[i] = GetDebounceTicks() - NAV_DEBOUNCE_TICKS;

    // If the previous controller let the sub screen go dark (video chaining,
    // L/R/X/Y skips), stay dark instead of flashing back on for a few seconds.
    if (sSubScreenOff)
    {
        _subScreenState = SUB_SCREEN_STATE_OFF;
        REG_MASTER_BRIGHT_SUB = 16 | (2 << 14);
        if (isDSiMode())
        {
            // On éteint intelligemment l'écran inactif
            if (gScreenSwapped) {
                powerOff(PM_BACKLIGHT_TOP);
                powerOn(PM_BACKLIGHT_BOTTOM); // On sécurise la vidéo
            } else {
                powerOff(PM_BACKLIGHT_BOTTOM);
                powerOn(PM_BACKLIGHT_TOP);
            }
            _subBacklightOff = true;
        }
    }
}

void PlayerController::RestoreSubScreen()
{
    sSubScreenOff = false;
    REG_MASTER_BRIGHT_SUB = 0;
    if (isDSiMode()) {
        // On force le rallumage des deux écrans pour être sûr
        powerOn(PM_BACKLIGHT_BOTTOM);
        powerOn(PM_BACKLIGHT_TOP);
    }
}

void PlayerController::Initialize()
{
    _view.Initialize();
    _playing = _player->isPlaying;

    // sync to whatever is physically held right now, so that a button the
    // user is still holding from just before this controller was created
    // (e.g. L/R/X/Y held a little past a video switch) doesn't get
    // misdetected as a brand new press on the very first Update() - see
    // InputProvider::PrimeCurrentState(). The repeater needs the same
    // treatment: PrimeCurrentState() only syncs _inputProvider, so without
    // this Reset() a LEFT/RIGHT held across the switch would still make
    // the freshly-constructed _inputRepeater fire an immediate spurious
    // repeat (see InputRepeater::Reset()).
    _inputProvider.PrimeCurrentState();
    _inputRepeater.Reset();

    _dimWaitFrames =
        Preferences::values[Preferences::Dim] * _player->fvHeader->fpsNum / _player->fvHeader->fpsDen;
    _dimFadeFrames = DIM_FADE_SEC * _player->fvHeader->fpsNum / _player->fvHeader->fpsDen;
    _invDimFadeFrames = 0x800000 / _dimFadeFrames;

    u32 totalSeconds = ((u64)_player->fvHeader->nrFrames * _player->fvHeader->fpsDen) / _player->fvHeader->fpsNum;
    _view.SetTotalTime(totalSeconds);
    _view.SetCurrentTime((u64)_player->curFrame * _player->fvHeader->fpsDen / _player->fvHeader->fpsNum);
}

void PlayerController::TogglePlayPause()
{
    if (_playing)
    {
        fv_pausePlayer(_player);
        _playing = false;
    }
    else
    {
        fv_resumePlayer(_player);
        _playing = true;
    }
}

void PlayerController::UpdateTouch()
{
    if (_subScreenState != SUB_SCREEN_STATE_ACTIVE)
        return;

    touchPosition touch;
    touchRead(&touch);

    // Si on a inversé les écrans, l'écran tactile (en bas physiquement)
    // affiche la vidéo, et non plus l'interface. On bloque donc les 
    // clics sur les boutons invisibles et on se contente de réveiller l'écran.
    if (gScreenSwapped) {
        if (_inputProvider.Triggered(KEY_TOUCH)) {
            _pendingNavAction = NAV_ACTION_SHOW_INFO;
        }
        return;
    }

    using namespace PlayerControls;
    if (_inputProvider.Triggered(KEY_TOUCH))
    {
        if (Timeline.Contains(touch.px, touch.py))
        {
            _seekPenDown = true;
            _seekLastFrame = -1;
        }
        else
        {
            _touchButton = Hit(touch.px, touch.py);
            _view.SetPressed(_touchButton);
        }
    }
    if (_inputProvider.Released(KEY_TOUCH))
    {
        if (_seekPenDown && _seekLastFrame >= 0) SeekFrame(_seekLastFrame);
        _seekPenDown = false;
        switch (_touchButton)
        {
            case Back: _pendingNavAction = NAV_ACTION_EXIT; break;
            case Repeat: _pendingNavAction = NAV_ACTION_TOGGLE_LOOP; break;
            case Previous: _pendingNavAction = NAV_ACTION_PREV; break;
            case PlayPause: TogglePlayPause(); break;
            case Next: _pendingNavAction = NAV_ACTION_NEXT; break;
            case Options: _pendingNavAction = NAV_ACTION_OPTIONS; break;
            default: break;
        }
        _touchButton = None;
        _view.SetPressed(None);
    }
    if (_inputProvider.Current(KEY_TOUCH) && _touchButton != None &&
        !Buttons[_touchButton].Contains(touch.px, touch.py))
    {
        _touchButton = None;
        _view.SetPressed(None);
    }

    if (_inputProvider.Current(KEY_TOUCH) && _seekPenDown)
    {
        int frame = (int)((int64_t)((int)touch.px - 16) * _player->fvHeader->nrFrames / 224);
        if (frame < 0)
            frame = 0;
        else if ((u32)frame >= _player->fvHeader->nrFrames)
            frame = _player->fvHeader->nrFrames - 1;
        if (frame != _seekLastFrame)
        {
            _seekLastFrame = frame;
            unsigned seconds = (u64)frame * _player->fvHeader->fpsDen / _player->fvHeader->fpsNum;
            char preview[32];
            snprintf(preview, sizeof(preview), "%02u:%02u", seconds / 60, seconds % 60);
            ShowMessage(Preferences::Text("Seek destination", "Destination"), preview);
        }
    }
}

void PlayerController::UpdateKeys()
{
    // Debounce indépendant par touche : chaque bouton a son propre
    // timestamp, donc appuyer sur une touche ne bloque pas les autres.
    u32 now = GetDebounceTicks();

    bool bDebounced      = (now - _lastNavActionVBlank[0]) < NAV_DEBOUNCE_TICKS;
    bool startDebounced  = (now - _lastNavActionVBlank[1]) < NAV_DEBOUNCE_TICKS;
    bool selectDebounced = (now - _lastNavActionVBlank[2]) < NAV_DEBOUNCE_TICKS;
    bool nextDebounced   = (now - _lastNavActionVBlank[3]) < NAV_DEBOUNCE_TICKS;
    bool prevDebounced   = (now - _lastNavActionVBlank[4]) < NAV_DEBOUNCE_TICKS;

    if (!bDebounced && _inputProvider.Triggered(KEY_B))
    {
        _pendingNavAction = NAV_ACTION_EXIT;
        _lastNavActionVBlank[0] = now;
        return;
    }

    // START opens the same options panel as in the browser. Keep the
    // optional picture/screen shortcuts when a direction is already held.
    if (!startDebounced && _inputProvider.Triggered(KEY_START))
    {
        _touchButton = PlayerControls::None;
        _view.SetPressed(PlayerControls::None);
        _seekPenDown = false;
        _lastNavActionVBlank[1] = now;
        if (_inputProvider.Current(KEY_UP))
        {
            gScreenSwapped = !gScreenSwapped;
            Preferences::values[Preferences::Screen] = gScreenSwapped;
            if (gScreenSwapped) {
                lcdMainOnBottom();
            } else {
                lcdMainOnTop();
            }
            
            // On réveille l'interface pour ne pas chercher les boutons à l'aveugle
            _subScreenState = SUB_SCREEN_STATE_ACTIVE;
            _subScreenStateCounter = 0;
            sSubScreenOff = false;
            
            if (isDSiMode()) {
                powerOn(PM_BACKLIGHT_BOTTOM);
                powerOn(PM_BACKLIGHT_TOP);
                _subBacklightOff = false;
            }
        } 
        else if (_inputProvider.Current(KEY_DOWN))
        {
            fv_scale_mode_t mode = fv_getScaleMode() == FV_SCALE_FIT ? FV_SCALE_STRETCH : FV_SCALE_FIT;
            fv_setScaleMode(_player, mode);
            Preferences::values[Preferences::Scale] = mode;
            ShowMessage(mode == FV_SCALE_STRETCH ? Preferences::Text("Picture: Stretch", "Image: Etirer")
                                                 : Preferences::Text("Picture: Fit", "Image: Adapter"),
                        Preferences::Text("Down+START to change", "Bas+START: changer"));
        }
        else
        {
            _pendingNavAction = NAV_ACTION_OPTIONS;
        }
        return;
    }
    if (!selectDebounced && _inputProvider.Triggered(KEY_SELECT))
    {
        _pendingNavAction = NAV_ACTION_TOGGLE_RANDOM;
        _lastNavActionVBlank[2] = now;
        return;
    }
    
    
    if (!nextDebounced && (_inputProvider.Triggered(KEY_R) || _inputProvider.Triggered(KEY_X)))
    {
        _pendingNavAction = NAV_ACTION_NEXT;
        _lastNavActionVBlank[3] = now;
        return;
    }
    if (!prevDebounced && (_inputProvider.Triggered(KEY_L) || _inputProvider.Triggered(KEY_Y)))
    {
        _pendingNavAction = NAV_ACTION_PREV;
        _lastNavActionVBlank[4] = now;
        return;
    }

    if (_inputProvider.Triggered(KEY_A))
    {
        TogglePlayPause();
    }
    else if (_inputProvider.Triggered(KEY_LEFT) || _inputRepeater.Triggered(KEY_LEFT) ||
             _inputProvider.Triggered(KEY_RIGHT) || _inputRepeater.Triggered(KEY_RIGHT))
    {
        bool forward = _inputProvider.Current(KEY_RIGHT);
        int step = (u64)Preferences::values[Preferences::Seek] * _player->fvHeader->fpsNum /
                   _player->fvHeader->fpsDen;
        int target = _player->curFrame + (forward ? step : -step);
        if (target < 0)
            target = 0;
        if ((u32)target >= _player->fvHeader->nrFrames)
            target = _player->fvHeader->nrFrames - 1;
        SeekFrame(target);
    }
}

void PlayerController::UpdateDim()
{
    // Wake-up sources for the sub screen: pausing, a touch tap, or START/SELECT
    // (options / shuffle controls). Navigation keys (D-pad, L/R/X/Y) and
    // automatic video chaining must NOT wake it: the screen stays dark.
    if (_captionsVisible || (!_playing && !_videoEnded) || _inputProvider.Triggered(KEY_TOUCH) ||
        _inputProvider.Triggered(KEY_START) || _inputProvider.Triggered(KEY_SELECT))
    {
        _subScreenState = SUB_SCREEN_STATE_ACTIVE;
        _subScreenStateCounter = 0;
        sSubScreenOff = false;
    }

    // Sur DS Lite/Phat, il n'y a qu'un seul contrôle de rétroéclairage
    // hardware qui affecte les DEUX écrans. powerOff(PM_BACKLIGHT_BOTTOM)
    // éteindrait aussi l'écran du haut (celui de la vidéo).
    // Sur DSi/3DS, les deux écrans sont contrôlés indépendamment.
    if (isDSiMode())
    {
        if (_subBacklightOff && _subScreenState != SUB_SCREEN_STATE_OFF)
        {
            // Allumage de l'interface
            if (gScreenSwapped) {
                powerOn(PM_BACKLIGHT_TOP);
            } else {
                powerOn(PM_BACKLIGHT_BOTTOM);
            }
            // Sécurise la vidéo
            powerOn(gScreenSwapped ? PM_BACKLIGHT_BOTTOM : PM_BACKLIGHT_TOP);
            
            _subBacklightOff = false;
        }
        else if (!_subBacklightOff && _subScreenState == SUB_SCREEN_STATE_OFF)
        {
            // Extinction intelligente
            if (gScreenSwapped) {
                powerOff(PM_BACKLIGHT_TOP); // L'interface (en haut) s'éteint
                powerOn(PM_BACKLIGHT_BOTTOM);
            } else {
                powerOff(PM_BACKLIGHT_BOTTOM); // L'interface (en bas) s'éteint
                powerOn(PM_BACKLIGHT_TOP);
            }
            _subBacklightOff = true;
        }
    }

    switch (_subScreenState)
    {
        case SUB_SCREEN_STATE_ACTIVE:
            REG_MASTER_BRIGHT_SUB = 0;
            if (_dimWaitFrames && ++_subScreenStateCounter >= _dimWaitFrames)
            {
                _subScreenStateCounter = 0;
                _subScreenState = SUB_SCREEN_STATE_DIMMING;
            }
            break;

        case SUB_SCREEN_STATE_DIMMING:
        {
            int dimFrame = (_subScreenStateCounter * 16 * _invDimFadeFrames + 0x400000) >> 23;
            if (dimFrame < 0)
                dimFrame = 0;
            else if (dimFrame > 16)
                dimFrame = 16;
            REG_MASTER_BRIGHT_SUB = dimFrame | (2 << 14);
            if (dimFrame < 16)
                _subScreenStateCounter++;
            else
            {
                _subScreenState = SUB_SCREEN_STATE_OFF;
                sSubScreenOff = true; // persist across controller recreations
            }
            break;
        }

        case SUB_SCREEN_STATE_OFF:
            break;
    }
}

PlayerController::NavAction PlayerController::Update()
{
    if (_player->videoEnded && _playing)
    {
        // the video reached its end: stop audio/playback cleanly (instead of
        // leaving the last audio buffer looping forever) and let the caller
        // decide what happens next (repeat / next / random, depending on
        // the loop/random flags it owns)
        fv_pausePlayer(_player);
        _playing = false;
        _videoEnded = true;
        _pendingNavAction = NAV_ACTION_VIDEO_ENDED;
    }

    if (_player->readError) _playing = false;
    _view.SetPlaying(_playing);
    if (_playing)
    {
        REG_DIVCNT = DIV_64_32;
        REG_DIV_NUMER = (u64)_player->curFrame * (u64)_player->fvHeader->fpsDen;
        REG_DIV_DENOM_L = _player->fvHeader->fpsNum;

        fv_updatePlayer(_player);

        while (REG_DIVCNT & DIV_BUSY)
            ;
        u32 time = REG_DIV_RESULT_L;
        if ((int)time != _lastTime)
        {
            _lastTime = time;
            _view.SetCurrentTime(time);
            _view.Update();
            _view.VBlank();
        }
    }
    else
    {
        _view.Update();
        swiWaitForVBlank();
        _view.VBlank();
        _lastTime = -1;
    }

    _inputProvider.Sample(); // todo: sample more frequently
    _inputProvider.Update();
    _inputRepeater.Update(&_inputProvider);
    if (_inputProvider.Current(KEY_LID) || _inputProvider.Released(KEY_LID))
    {
        // Lid changes take priority over touch releases, navigation and EOF.
        // Stay paused on reopening until the user makes a fresh input.
        if (_playing)
        {
            fv_pausePlayer(_player);
            _playing = false;
        }
        _seekPenDown = false;
        _touchButton = PlayerControls::None;
        _view.SetPressed(PlayerControls::None);
        _pendingNavAction = NAV_ACTION_NONE;
        _inputRepeater.Reset();
        if (_inputProvider.Released(KEY_LID))
            _inputProvider.PrimeCurrentState();
    }
    else
    {
        UpdateTouch();
        UpdateKeys();
    }
    UpdateDim();

    NavAction action = _pendingNavAction;
    _pendingNavAction = NAV_ACTION_NONE;
    return action;
}

void PlayerController::ShowMessage(const char* line1, const char* line2)
{
    _view.SetMessage(line1, line2);
    // while playing, PlayerView::Update()/VBlank() are normally only
    // called once a second (when the displayed second changes), to avoid
    // needless redraws; force an immediate refresh here so the toast (and
    // the time display drawn alongside it) doesn't wait for that next tick
    _view.Update();
    _view.VBlank();
}

void PlayerController::SetPaused(bool paused)
{
    if (paused == !_playing)
        return;
    TogglePlayPause();
}

void PlayerController::SeekFrame(u32 frame)
{
    bool wasPlaying = _playing;
    int previous = _player->curFrame;
    fv_gotoNearestKeyFrame(_player, frame);
    // Seeking forward must advance even with widely spaced keyframes.
    if (frame > (u32)previous && _player->curFrame <= previous &&
        (u32)(_player->lastKeyFrame + 1) < _player->fvHeader->nrKeyFrames)
        fv_gotoNextKeyFrame(_player);
    fv_startPlayer(_player);
    if (!wasPlaying)
    {
        // Pause the audio first; priming a still frame must not emit a burst.
        fv_pausePlayer(_player);
        for (int i = 0; i < 3 && !_player->readError; ++i)
            fv_updatePlayer(_player);
    }
    _playing = wasPlaying;
    unsigned seconds = (u64)_player->curFrame * _player->fvHeader->fpsDen / _player->fvHeader->fpsNum;
    char actual[32];
    snprintf(actual, sizeof(actual), "%02u:%02u", seconds / 60, seconds % 60);
    _view.SetCurrentTime(seconds);
    ShowMessage(Preferences::Text("Seek: keyframe", "Saut: image cle"), actual);
}
