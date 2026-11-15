#pragma once
#include <stddef.h>
namespace Storage
{
// Caller owns the returned heap buffer. Reads happen only outside playback.
char *ReadText(const char *path, size_t capacity, int *error = 0);
bool WriteState(const char *name, const char *text);
bool Exists(const char *path);
const char *Drive();
} // namespace Storage
