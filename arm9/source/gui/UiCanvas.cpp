#include <nds.h>
#include <string.h>
#include "UiCanvas.h"
#include "RobotoRegular10_ntft.h"
#include "RobotoMedium13_ntft.h"

UiCanvas::UiCanvas() : _body(RobotoRegular10_ntft), _heading(RobotoMedium13_ntft), _dirty(0) {}
UiCanvas &UiCanvas::Get()
{
    static UiCanvas canvas;
    return canvas;
}
void UiCanvas::Initialize()
{
    // 768 unique tiles use 24 KiB. Map 15 uses the last 2 KiB of bank H.
    // No new VRAM bank, sprite allocation or bitmap asset is required.
    REG_DISPCNT_SUB = MODE_0_2D;
    REG_BG2CNT_SUB = BG_32x32 | BG_COLOR_16 | BG_MAP_BASE(15) | BG_TILE_BASE(0);
    REG_BG2HOFS_SUB = REG_BG2VOFS_SUB = 0;
    for (int i = 0; i < 1024; ++i)
        BG_MAP_RAM_SUB(15)[i] = i < 768 ? i : 0;
    const u16 palette[] = {
        RGB15(3,5,7), RGB15(5,8,10), RGB15(8,12,14), RGB15(9,12,14),
        RGB15(15,19,21), RGB15(23,27,29), RGB15(30,31,31), RGB15(9,26,28),
        RGB15(4,10,12), RGB15(6,16,18), RGB15(8,22,24), RGB15(7,13,16),
        0, 0, 0, 0
    };
    for (unsigned i = 0; i < sizeof(palette) / sizeof(*palette); ++i)
        BG_PALETTE_SUB[i] = palette[i];
    REG_BLDCNT_SUB = 0;
    REG_DISPCNT_SUB = MODE_0_2D | DISPLAY_BG2_ACTIVE;
    Clear();
}
void UiCanvas::Clear()
{
    memset(_pixels, 0, sizeof(_pixels));
    _dirty = 0xffffff;
}
void UiCanvas::Pixel(int x, int y, Color color)
{
    if ((unsigned)x >= 256 || (unsigned)y >= 192)
        return;
    unsigned index = ((y / 8) * 32 + x / 8) * 16 + (y % 8) * 2 + (x % 8) / 4;
    unsigned shift = (x & 3) * 4;
    u16 next = (_pixels[index] & ~(15 << shift)) | ((unsigned)color << shift);
    if (_pixels[index] != next)
    {
        _pixels[index] = next;
        _dirty |= 1u << (y / 8);
    }
}
void UiCanvas::Fill(int x, int y, int width, int height, Color color)
{
    int right = x + width < 256 ? x + width : 256;
    int bottom = y + height < 192 ? y + height : 192;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    for (int row = y; row < bottom; ++row)
        for (int col = x; col < right; ++col)
            Pixel(col, row, color);
}
void UiCanvas::Circle(int x, int y, int radius, Color color)
{
    for (int row = -radius; row <= radius; ++row)
        for (int col = -radius; col <= radius; ++col)
            if (col * col + row * row <= radius * radius)
                Pixel(x + col, y + row, color);
}
int UiCanvas::Width(const char *text, Face face) const
{
    int width, height;
    (face == Heading ? _heading : _body).MeasureString(text, width, height);
    return width;
}
void UiCanvas::TextAt(int x, int y, const char *text, Color color, int clipLeft, int clipRight, Face face)
{
    const NtftFont &font = face == Heading ? _heading : _body;
    int height = font.GetFontHeight();
    if (height > 20) return;
    // Render one glyph at a time so long titles can scroll without a large
    // temporary string bitmap. Newlines are handled by the caller's layout.
    int pen = 0;
    for (const unsigned char *s = (const unsigned char *)text; *s; ++s)
    {
        unsigned char ch = *s < 32 ? ' ' : *s;
        const ntft_cinfo_char_t *info = font.GetCharInfo(ch);
        int begin = info->characterBeginOffset;
        int leading = pen + begin >= 0 ? begin : 0;
        int advance = leading + (int)info->characterWidth + info->characterEndOffset;
        if (advance < 1) advance = 1;
        int glyphX = x + leading;
        if (glyphX + (int)info->characterWidth >= clipLeft && glyphX < clipRight)
        {
            char glyph[] = {(char)ch, 0};
            memset(_glyph, 0, 256 * height);
            font.CreateStringData(glyph, _glyph, 256);
            int scratchLeft = begin > 0 ? begin : 0;
            int width = info->characterWidth;
            if (width > 256 - scratchLeft) width = 256 - scratchLeft;
            for (int row = 0; row < height; ++row)
                for (int col = 0; col < width; ++col)
                {
                    int px = glyphX + col;
                    unsigned coverage = _glyph[row * 256 + scratchLeft + col];
                    if (!coverage || px < clipLeft || px >= clipRight) continue;
                    int level = (coverage * 4 + 254) / 255;
                    static const Color white[] = {Background, Faint, Muted, Text, Bright};
                    static const Color muted[] = {Background, Line, Faint, Muted, Muted};
                    static const Color accent[] = {Background, AccentFaint, AccentMid, AccentLight, Accent};
                    Pixel(px, y + row, color == Accent ? accent[level]
                                         : color == Muted ? muted[level] : white[level]);
                }
        }
        x += advance;
        pen += advance;
        if (x >= clipRight) break;
    }
}
void UiCanvas::Centered(int x, int y, const char *text, Color color)
{
    TextAt(x - Width(text) / 2, y, text, color);
}
void UiCanvas::Present()
{
    if (!_dirty) return;
    for (int row = 0; row < 24; ++row)
        if (_dirty & (1u << row))
        {
            const u16 *source = _pixels + row * 512;
            DC_FlushRange(source, 1024);
            dmaCopyWords(3, source, BG_TILE_RAM_SUB(0) + row * 512, 1024);
        }
    _dirty = 0;
}
