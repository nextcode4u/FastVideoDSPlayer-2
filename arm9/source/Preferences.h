#pragma once
#include <stddef.h>
namespace Preferences
{
enum Key
{
    Language,
    Scale,
    Screen,
    Repeat,
    Shuffle,
    Seek,
    Dim,
    Volume,
    Mute,
    Mono,
    AudioDelay,
    SubtitleDelay,
    Subtitles,
    Diagnostics,
    Resume,
    EndAction,
    PerVideo,
    Count
};
struct Option
{
    const char *key, *english, *french;
    int initial, minimum, maximum, step;
};
extern const Option options[Count];
extern int values[Count];
extern char videoRoot[256], returnPath[256];
void Defaults(const char *drive);
void Parse(char *text);
void Load();
void BeginVideo(int scale, int audioDelay, int subtitleDelay, bool hasOverride);
void EndVideo();
bool Save();
void Change(Key key, int direction);
void ValueText(Key key, char *out, size_t capacity);
const char *Text(const char *english, const char *french);
} // namespace Preferences
