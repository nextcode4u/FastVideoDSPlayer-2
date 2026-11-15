#include <nds.h>
#include "ControlSurface.h"

namespace ControlSurface
{
// BG0/BG1 use the original playback art below 4 KiB. BG2 uses map 2
// and console characters at 16 KiB. This surface occupies map 3 and
// 192 bytes of tiles at 8 KiB, within the existing 32 KiB VRAM bank.
static const int FirstTile = 256;
void Fill(int x, int y, int width, int height, Color color)
{
    volatile u16 *map = BG_MAP_RAM_SUB(3);
    for (int row = y; row < y + height && row < 24; ++row)
        for (int column = x; column < x + width && column < 32; ++column)
            if (row >= 0 && column >= 0)
                map[row * 32 + column] = (14 << 12) | (FirstTile + color);
}

void Initialize()
{
    volatile u16 *tiles = BG_TILE_RAM_SUB(0) + FirstTile * 16;
    for (int tile = Clear; tile <= Divider; ++tile)
        for (int halfword = 0; halfword < 16; ++halfword)
            tiles[tile * 16 + halfword] = tile * 0x1111;
    BG_PALETTE_SUB[14 * 16 + Header] = RGB15(4, 6, 8);
    BG_PALETTE_SUB[14 * 16 + Panel] = RGB15(6, 8, 11);
    BG_PALETTE_SUB[14 * 16 + Button] = RGB15(8, 12, 16);
    BG_PALETTE_SUB[14 * 16 + Selected] = RGB15(7, 17, 20);
    BG_PALETTE_SUB[14 * 16 + Divider] = RGB15(11, 16, 20);
    BG_PALETTE_SUB[7 * 16 + 15] = RGB15(31, 31, 31);
    Fill(0, 0, 32, 24, Clear);
    REG_BG3CNT_SUB = BG_32x32 | BG_COLOR_16 | BG_PRIORITY_1 | BG_MAP_BASE(3) | BG_TILE_BASE(0);
    REG_BG3HOFS_SUB = REG_BG3VOFS_SUB = 0;
    REG_DISPCNT_SUB |= DISPLAY_BG3_ACTIVE;
    SUB_WIN_IN |= 1 << 3;
    SUB_WIN_OUT |= 1 << 3;
}
} // namespace ControlSurface
