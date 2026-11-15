#pragma once
#include <stddef.h>
#include <stdint.h>
namespace MediaText
{
struct Cue
{
    uint32_t start, end;
    size_t text, length;
};
bool ResolvePath(const char *baseFile, const char *entry, char *out, size_t capacity);
size_t Playlist(char *text, const char *path, char (*items)[256], size_t capacity, size_t &skipped);
size_t Subtitles(char *text, Cue *cues, size_t capacity, size_t &skipped);
int ActiveCue(const Cue *cues, size_t count, uint32_t milliseconds);
void DisplayText(const char *utf8, size_t length, char *out, size_t capacity);
} // namespace MediaText
