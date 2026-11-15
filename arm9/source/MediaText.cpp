#include "MediaText.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <ctype.h>
namespace MediaText
{
bool ResolvePath(const char *baseFile, const char *entry, char *out, size_t capacity)
{
    if (!baseFile || !entry || !*entry || strstr(entry, "://") || strchr(entry, '\n') || strchr(entry, '\r'))
        return false;
    char combined[768];
    const char *colon = strchr(entry, ':');
    if (colon)
    {
        if (strncmp(entry, "sd:/", 4) && strncmp(entry, "fat:/", 5))
            return false;
        if (strlen(entry) >= sizeof(combined))
            return false;
        strcpy(combined, entry);
    }
    else
    {
        const char *baseColon = strchr(baseFile, ':'), *slash = strrchr(baseFile, '/');
        if (!baseColon || !slash)
            return false;
        size_t prefix = entry[0] == '/' ? (size_t)(baseColon - baseFile + 1) : (size_t)(slash - baseFile + 1);
        if (prefix + strlen(entry) >= sizeof(combined))
            return false;
        memcpy(combined, baseFile, prefix);
        strcpy(combined + prefix, entry);
    }
    const char *drive = strchr(combined, ':');
    if (!drive)
        return false;
    size_t used = drive - combined + 1;
    if (used + 2 > capacity)
        return false;
    memcpy(out, combined, used);
    out[used++] = '/';
    out[used] = 0;
    size_t floor = used;
    char *context = 0;
    for (char *part = strtok_r(combined + floor, "/\\", &context); part; part = strtok_r(0, "/\\", &context))
    {
        if (!strcmp(part, "."))
            continue;
        if (!strcmp(part, ".."))
        {
            if (used == floor)
                return false;
            while (used > floor && out[used - 1] != '/')
                --used;
            if (used > floor)
                --used;
            out[used] = 0;
            continue;
        }
        if (strchr(part, ':') || strchr(part, '\t'))
            return false;
        size_t len = strlen(part);
        if (used + len + 2 > capacity)
            return false;
        if (used > floor)
            out[used++] = '/';
        memcpy(out + used, part, len + 1);
        used += len;
    }
    return used > floor;
}
size_t Playlist(char *text, const char *path, char (*items)[256], size_t capacity, size_t &skipped)
{
    size_t count = 0;
    skipped = 0;
    if (!strncmp(text, "\xef\xbb\xbf", 3))
        text += 3;
    char *context = 0;
    for (char *line = strtok_r(text, "\n", &context); line; line = strtok_r(0, "\n", &context))
    {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\r')
            line[--len] = 0;
        if (!*line || *line == '#')
            continue;
        char pathOut[256];
        const char *ext = strrchr(line, '.');
        if (!ext || strcasecmp(ext, ".fv") || !ResolvePath(path, line, pathOut, sizeof(pathOut)) ||
            count == capacity)
        {
            ++skipped;
            continue;
        }
        strcpy(items[count++], pathOut);
    }
    return count;
}
static bool Number(const char *&p, const char *end, unsigned maximumDigits, unsigned &value)
{
    value = 0;
    unsigned digits = 0;
    while (p < end && *p >= '0' && *p <= '9')
    {
        if (++digits > maximumDigits)
            return false;
        value = value * 10 + (*p++ - '0');
    }
    return digits != 0;
}
static bool Time(const char *&p, const char *end, uint32_t &value)
{
    unsigned h, m, s, ms;
    if (!Number(p, end, 3, h) || p == end || *p++ != ':' || !Number(p, end, 2, m) || p == end ||
        *p++ != ':' || !Number(p, end, 2, s) || p == end || *p++ != ',' || !Number(p, end, 3, ms) || m > 59 ||
        s > 59)
        return false;
    value = ((h * 60 + m) * 60 + s) * 1000 + ms;
    return true;
}
static bool Timestamp(const char *p, const char *end, uint32_t &start, uint32_t &finish)
{
    if (!Time(p, end, start))
        return false;
    while (p < end && *p == ' ')
        ++p;
    if (end - p < 3 || memcmp(p, "-->", 3))
        return false;
    p += 3;
    while (p < end && *p == ' ')
        ++p;
    return Time(p, end, finish) && finish > start;
}
size_t Subtitles(char *text, Cue *cues, size_t capacity, size_t &skipped)
{
    size_t count = 0;
    skipped = 0;
    char *line = text;
    while (*line)
    {
        char *end = strchr(line, '\n');
        if (!end)
            end = line + strlen(line);
        uint32_t startTime, endTime;
        if (Timestamp(line, end, startTime, endTime))
        {
            char *body = *end ? end + 1 : end, *tail = body;
            while (*tail)
            {
                char *next = strchr(tail, '\n');
                if (!next)
                {
                    tail += strlen(tail);
                    break;
                }
                if (next == tail || (next == tail + 1 && *tail == '\r'))
                    break;
                tail = next + 1;
            }
            if (count < capacity && (!count || startTime >= cues[count - 1].start))
                cues[count++] = {startTime, endTime, (size_t)(body - text), (size_t)(tail - body)};
            else
                ++skipped;
            line = *tail ? tail + 1 : tail;
        }
        else
            line = *end ? end + 1 : end;
    }
    return count;
}
int ActiveCue(const Cue *cues, size_t count, uint32_t milliseconds)
{
    size_t lo = 0, hi = count;
    while (lo < hi)
    {
        size_t mid = (lo + hi) / 2;
        if (cues[mid].start <= milliseconds)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (!lo)
        return -1;
    return milliseconds < cues[lo - 1].end ? (int)lo - 1 : -1;
}
void DisplayText(const char *text, size_t length, char *out, size_t capacity)
{
    if (!capacity)
        return;
    size_t used = 0;
    bool tag = false;
    for (size_t i = 0; i < length && used + 1 < capacity; ++i)
    {
        unsigned char ch = text[i];
        if (ch == '<')
        {
            tag = true;
            continue;
        }
        if (ch == '>')
        {
            tag = false;
            continue;
        }
        if (tag)
            continue;
        if (ch == '\r')
            continue;
        if (ch < 32)
        {
            if (ch == '\n')
                out[used++] = ' ';
            continue;
        }
        if (ch < 128)
        {
            out[used++] = ch;
            continue;
        }
        unsigned code = 0, extra = 0;
        if ((ch & 0xe0) == 0xc0)
        {
            code = ch & 31;
            extra = 1;
        }
        else if ((ch & 0xf0) == 0xe0)
        {
            code = ch & 15;
            extra = 2;
        }
        else if ((ch & 0xf8) == 0xf0)
        {
            code = ch & 7;
            extra = 3;
        }
        bool valid = extra && i + extra < length;
        for (unsigned j = 1; valid && j <= extra; ++j)
        {
            unsigned char c = text[i + j];
            valid = (c & 0xc0) == 0x80;
            code = (code << 6) | (c & 63);
        }
        if (valid)
            i += extra;
        const char *replacement = "?";
        if (valid)
        {
            if (code >= 0xc0 && code <= 0xff)
            {
                if ((code >= 0xc0 && code <= 0xc5) || (code >= 0xe0 && code <= 0xe5))
                    replacement = "a";
                else if (code == 0xc7 || code == 0xe7)
                    replacement = "c";
                else if ((code >= 0xc8 && code <= 0xcb) || (code >= 0xe8 && code <= 0xeb))
                    replacement = "e";
                else if ((code >= 0xcc && code <= 0xcf) || (code >= 0xec && code <= 0xef))
                    replacement = "i";
                else if (code == 0xd1 || code == 0xf1)
                    replacement = "n";
                else if ((code >= 0xd2 && code <= 0xd6) || (code >= 0xf2 && code <= 0xf6))
                    replacement = "o";
                else if ((code >= 0xd9 && code <= 0xdc) || (code >= 0xf9 && code <= 0xfc))
                    replacement = "u";
                else if (code == 0xdf)
                    replacement = "ss";
            }
            else if (code == 0x2019 || code == 0x2018)
                replacement = "'";
            else if (code == 0x201c || code == 0x201d)
                replacement = "\"";
            else if (code == 0x2013 || code == 0x2014)
                replacement = "-";
        }
        while (*replacement && used + 1 < capacity)
            out[used++] = *replacement++;
    }
    out[used] = 0;
}
} // namespace MediaText
