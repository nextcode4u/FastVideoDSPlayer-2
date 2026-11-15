#pragma once
#include <cstdint>
#include <cstring>
using u8 = uint8_t; using u16 = uint16_t; using u32 = uint32_t;
using s32 = int32_t; using u64 = uint64_t;
#define ALIGN(n) __attribute__((aligned(n)))
#define RGB15(r,g,b) ((r) | ((g)<<5) | ((b)<<10))
#define BG_MAP_BASE(n) ((n)<<8)
#define BG_TILE_BASE(n) ((n)<<2)
#define MODE_0_2D 0
#define BG_32x32 0
#define BG_COLOR_16 0
#define DISPLAY_BG2_ACTIVE (1<<10)
extern u16 testVram[16384], testPalette[256];
extern unsigned testUploads;
extern u32 REG_DISPCNT_SUB;
extern u16 REG_BG2CNT_SUB, REG_BG2HOFS_SUB, REG_BG2VOFS_SUB, REG_BLDCNT_SUB;
#define BG_MAP_RAM_SUB(n) (testVram + (n)*1024)
#define BG_TILE_RAM_SUB(n) (testVram + (n)*8192)
#define BG_PALETTE_SUB testPalette
inline void DC_FlushRange(const void*,unsigned) {}
inline void dmaCopyWords(int, const void* from, void* to, unsigned size)
{
    ++testUploads;
    std::memcpy(to, from, size);
}
