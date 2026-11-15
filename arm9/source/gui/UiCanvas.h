#pragma once
#include <nds.h>
#include "core/NtftFont.h"

// One cached 4-bit tiled screen, shared by playback and its menus. Changed
// 8-pixel bands are uploaded; idle controls do not redraw the whole display.
class UiCanvas
{
public:
    enum Color { Background, Surface, Line, Faint, Muted, Text, Bright, Accent,
                 AccentFaint, AccentMid, AccentLight, Pressed, Reserved12, Reserved13,
                 Reserved14, Reserved15 };
    enum Face { Body, Heading };
    static UiCanvas &Get();
    void Initialize();
    void Clear();
    void Fill(int x, int y, int width, int height, Color color);
    void Pixel(int x, int y, Color color);
    void Circle(int x, int y, int radius, Color color);
    int Width(const char *text, Face face = Body) const;
    void TextAt(int x, int y, const char *text, Color color = Text,
                int clipLeft = 0, int clipRight = 256, Face face = Body);
    void Centered(int x, int y, const char *text, Color color = Text);
    void Present();
private:
    UiCanvas();
    NtftFont _body, _heading;
    u16 _pixels[256 * 192 / 4] ALIGN(32);
    u8 _glyph[256 * 20];
    u32 _dirty;
};
