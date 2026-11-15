#include <nds.h>
#include <cassert>
#include <string>
#include "../arm9/source/gui/UiCanvas.h"
#include "../arm9/source/gui/PlayerView.h"
#include "../arm9/source/Preferences.h"
#include "../arm9/source/Storage.h"

u16 testVram[16384], testPalette[256];
unsigned testUploads;
u32 REG_DISPCNT_SUB;
u16 REG_BG2CNT_SUB, REG_BG2HOFS_SUB, REG_BG2VOFS_SUB, REG_BLDCNT_SUB;
static u32 ticks;
u32 GetDebounceTicks() { return ticks; }
namespace Storage
{
const char *Drive() { return "sd:"; }
char *ReadText(const char *, size_t, int *) { return nullptr; }
bool WriteState(const char *, const char *) { return true; }
}
static unsigned pixel(int x, int y)
{
    unsigned at = ((y / 8) * 32 + x / 8) * 16 + (y % 8) * 2 + (x % 8) / 4;
    return (testVram[at] >> ((x & 3) * 4)) & 15;
}
int main()
{
    auto &ui = UiCanvas::Get();
    ui.Initialize(); ui.Present();
    assert(testUploads == 24);
    testUploads = 0; ui.Present(); assert(testUploads == 0);
    ui.Fill(-5, -3, 7, 5, UiCanvas::Accent); ui.Present();
    assert(testUploads == 1);
    assert(pixel(0, 0) == UiCanvas::Accent && pixel(1, 1) == UiCanvas::Accent);
    assert(pixel(2, 2) == UiCanvas::Background);
    ui.Clear(); ui.Present();
    std::string longText(1024, 'W');
    ui.TextAt(-230, 185, longText.c_str(), UiCanvas::Bright, 16, 240, UiCanvas::Heading);
    ui.Present();
    for (int y = 185; y < 192; ++y)
    {
        for (int x = 0; x < 16; ++x) assert(pixel(x,y) == UiCanvas::Background);
        for (int x = 240; x < 256; ++x) assert(pixel(x,y) == UiCanvas::Background);
    }
    for (int i = 0; i < 768; ++i) assert(BG_MAP_RAM_SUB(15)[i] == i);
    // Exercise the actual layout with the shipped proportional font data.
    Preferences::Defaults("sd:");
    PlayerView view;
    view.Initialize(); view.SetTotalTime(7200); view.SetCurrentTime(130);
    view.SetPlaying(true);
    view.SetDetails("Short film.fv", "A caption remains visible while controls change.", "");
    testUploads = 0;
    view.SetDetails("Short film.fv", "A caption remains visible while controls change.", "");
    view.Update(); view.VBlank(); assert(testUploads == 0);
    u16 caption[6 * 512];
    std::memcpy(caption, testVram + 7 * 512, sizeof(caption));
    view.SetMessage("Repeat on", "Shuffle off"); view.Update(); view.VBlank();
    assert(!std::memcmp(caption, testVram + 7 * 512, sizeof(caption)));
    for (int b = PlayerControls::Back; b <= PlayerControls::Options; ++b)
        view.SetPressed((PlayerControls::Button)b);
    view.SetPressed(PlayerControls::None);
    view.SetPlaying(false); view.Update(); view.VBlank();
    view.SetDetails(longText.c_str(), longText.c_str(), "Late 0 AV +1ms Q24 U0");
    for (int i = 0; i < 100; ++i)
    {
        ticks += 3272;
        view.Update(); view.VBlank();
    }
    assert(ui.Width("iiii") < ui.Width("WWWW"));
}
