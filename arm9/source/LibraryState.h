#pragma once
#include <stdint.h>
namespace LibraryState
{
struct Record
{
    char path[256];
    uint32_t frame, bookmark, frames;
    int scale, audioDelay, subtitleDelay;
};
extern Record recent[32];
extern int recentCount;
void Load();
Record *Find(const char *path);
void Remember(const char *path, uint32_t frame, uint32_t frames, bool finished);
bool Save();
} // namespace LibraryState
