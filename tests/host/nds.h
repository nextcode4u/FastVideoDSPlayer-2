#pragma once
// Minimal host substitutes for the controller's hardware and drawing calls.
#include <cstdint>
#include <cstring>
using u8 = uint8_t; using s8 = int8_t;
using u16 = uint16_t; using s16 = int16_t;
using u32 = uint32_t; using s32 = int32_t; using u64 = uint64_t;
#define ALIGN(n) __attribute__((aligned(n)))
struct SpriteEntry { u16 attribute[4]; };
struct SpriteRotation { s16 values[4]; };
struct OAMTable { SpriteEntry oamBuffer[128]; SpriteRotation matrixBuffer[32]; };
struct touchPosition { u16 px, py; };
enum {
    KEY_A = 1, KEY_B = 2, KEY_SELECT = 4, KEY_START = 8,
    KEY_RIGHT = 16, KEY_LEFT = 32, KEY_UP = 64, KEY_DOWN = 128,
    KEY_R = 256, KEY_L = 512, KEY_X = 1024, KEY_Y = 2048,
    KEY_TOUCH = 4096, KEY_LID = 8192,
    PM_BACKLIGHT_TOP = 1, PM_BACKLIGHT_BOTTOM = 2,
    DIV_64_32 = 1, DIV_BUSY = 0x8000
};
extern u16 testKeys;
extern touchPosition testTouch;
extern u16 REG_MASTER_BRIGHT_SUB, REG_DIVCNT;
extern u64 REG_DIV_NUMER;
extern u32 REG_DIV_DENOM_L, REG_DIV_RESULT_L;
inline u16 keysCurrent() { return testKeys; }
inline void touchRead(touchPosition* p) { *p = testTouch; }
inline bool isDSiMode() { return true; }
inline void powerOn(int) {}
inline void powerOff(int) {}
inline void lcdMainOnTop() {}
inline void lcdMainOnBottom() {}
inline void swiWaitForVBlank() {}
inline void DC_FlushRange(const void*, unsigned) {}
inline void dmaCopyWords(int, const void*, void*, unsigned) {}
