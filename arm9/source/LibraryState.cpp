#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "LibraryState.h"
#include "Preferences.h"
#include "Storage.h"
namespace LibraryState
{
Record recent[32];
int recentCount;
Record *Find(const char *path)
{
    for (int i = 0; i < recentCount; ++i)
        if (!strcmp(path, recent[i].path))
            return &recent[i];
    return 0;
}
void Load()
{
    recentCount = 0;
    char path[96];
    snprintf(path, sizeof(path), "%s/data/fastvideods/history.ini", Storage::Drive());
    char *text = Storage::ReadText(path, 16384);
    if (!text)
    {
        strcat(path, ".bak");
        text = Storage::ReadText(path, 16384);
    }
    if (!text)
        return;
    char *context = 0;
    for (char *line = strtok_r(text, "\n", &context); line && recentCount < 32;
         line = strtok_r(0, "\n", &context))
    {
        size_t lineLength = strlen(line);
        if (lineLength && line[lineLength - 1] == '\r') line[lineLength - 1] = 0;
        Record record = {};
        long fields[6];
        char *field = line;
        bool valid = true;
        for (unsigned i = 0; i < 6; ++i)
        {
            char *end;
            errno = 0;
            fields[i] = strtol(field, &end, 10);
            if (end == field || errno == ERANGE || *end != (i == 5 ? '\t' : ','))
            {
                valid = false;
                break;
            }
            field = end + 1;
        }
        if (!valid || fields[0] < 0 || fields[1] < 0 || fields[2] <= 0 || fields[2] >= 0x0fffffff ||
            fields[0] >= fields[2] || fields[1] >= fields[2] || fields[3] < 0 || fields[3] > 1 ||
            fields[4] < -500 || fields[4] > 500 || fields[5] < -5000 || fields[5] > 5000)
            continue;
        unsigned frame = fields[0], mark = fields[1], frames = fields[2];
        record.scale = fields[3];
        record.audioDelay = fields[4];
        record.subtitleDelay = fields[5];
        const char *name = field;
        if (!*name || strlen(name) >= sizeof(record.path) || strchr(name, '\t') || strchr(name, '\r'))
            continue;
        if (strncmp(name, "sd:/", 4) && strncmp(name, "fat:/", 5))
            continue;
        if (Find(name))
            continue;
        strcpy(record.path, name);
        record.frame = frame;
        record.bookmark = mark;
        record.frames = frames;
        recent[recentCount++] = record;
    }
    free(text);
}
void Remember(const char *path, uint32_t frame, uint32_t frames, bool finished)
{
    if (!frames || strlen(path) >= 256)
        return;
    Record *existing = Find(path), record = {};
    int index = existing ? (int)(existing - recent) : recentCount;
    if (existing && existing->frames == frames)
        record = *existing;
    if (index > 31)
        index = 31;
    memmove(recent + 1, recent, index * sizeof(Record));
    if (recentCount < 32 && !existing)
        ++recentCount;
    strcpy(record.path, path);
    record.frame = finished ? 0 : frame < frames ? frame : 0;
    record.frames = frames;
    record.scale = Preferences::values[Preferences::Scale];
    record.audioDelay = Preferences::values[Preferences::AudioDelay];
    record.subtitleDelay = Preferences::values[Preferences::SubtitleDelay];
    recent[0] = record;
}
bool Save()
{
    char *text = (char *)malloc(16384);
    if (!text)
        return false;
    size_t used = 0;
    for (int i = 0; i < recentCount; ++i)
    {
        const Record &r = recent[i];
        used += snprintf(text + used, 16384 - used, "%lu,%lu,%lu,%d,%d,%d\t%s\n", (unsigned long)r.frame,
                         (unsigned long)r.bookmark, (unsigned long)r.frames, r.scale, r.audioDelay,
                         r.subtitleDelay, r.path);
    }
    bool ok = Storage::WriteState("history.ini", text);
    free(text);
    return ok;
}
} // namespace LibraryState
