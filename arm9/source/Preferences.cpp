#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "Preferences.h"
#include "Storage.h"
namespace Preferences
{
const Option options[Count] = {
    {"language", "Language", "Langue", 0, 0, 1, 1},
    {"scale", "Picture", "Image", 0, 0, 1, 1},
    {"screen", "Video screen", "Ecran video", 0, 0, 1, 1},
    {"repeat", "Repeat", "Boucle", 0, 0, 1, 1},
    {"shuffle", "Shuffle", "Aleatoire", 0, 0, 2, 1},
    {"seek_seconds", "Seek seconds", "Saut secondes", 10, 5, 30, 5},
    {"dim_seconds", "Dim after (s)", "Veille apres (s)", 10, 0, 60, 5},
    {"volume", "Volume", "Volume", 127, 0, 127, 8},
    {"mute", "Mute", "Muet", 0, 0, 1, 1},
    {"mono", "Mono audio", "Audio mono", 0, 0, 1, 1},
    {"audio_delay_ms", "Sound delay ms", "Retard son ms", 0, -500, 500, 25},
    {"subtitle_delay_ms", "Caption delay ms", "Retard texte ms", 0, -5000, 5000, 250},
    {"subtitles", "Subtitles", "Sous-titres", 1, 0, 1, 1},
    {"diagnostics", "Diagnostics", "Diagnostic", 0, 0, 1, 1},
    {"resume", "Offer resume", "Proposer reprise", 1, 0, 1, 1},
    {"end_action", "At end", "A la fin", 1, 0, 3, 1},
    {"per_video", "Remember per video", "Retenir par video", 0, 0, 1, 1}};
int values[Count];
static bool videoActive;
static int playbackDefaults[3];
static const Key playbackKeys[3] = {Scale, AudioDelay, SubtitleDelay};
void BeginVideo(int scale, int audioDelay, int subtitleDelay, bool hasOverride)
{
    const int overrides[3] = {scale, audioDelay, subtitleDelay};
    for (unsigned i = 0; i < 3; ++i)
    {
        playbackDefaults[i] = values[playbackKeys[i]];
        if (hasOverride && values[PerVideo])
            values[playbackKeys[i]] = overrides[i];
    }
    videoActive = true;
}
void EndVideo()
{
    if (videoActive && values[PerVideo])
        for (unsigned i = 0; i < 3; ++i)
            values[playbackKeys[i]] = playbackDefaults[i];
    videoActive = false;
}
char videoRoot[256], returnPath[256];
const char *Text(const char *en, const char *fr)
{
    return values[Language] ? fr : en;
}
void Defaults(const char *drive)
{
    for (int i = 0; i < Count; ++i)
        values[i] = options[i].initial;
    snprintf(videoRoot, sizeof(videoRoot), "%s/media/videos", drive);
    returnPath[0] = 0;
}
static bool Path(const char *text)
{
    return strlen(text) < 256 && (strncmp(text, "sd:/", 4) == 0 || strncmp(text, "fat:/", 5) == 0) &&
           !strchr(text, '\r') && !strchr(text, '\n') && !strchr(text, '\t') && !strstr(text, "/../");
}
void Parse(char *text)
{
    char *context = 0;
    for (char *line = strtok_r(text, "\n", &context); line; line = strtok_r(0, "\n", &context))
    {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\r')
            line[--len] = 0;
        char *equal = strchr(line, '=');
        if (!equal)
            continue;
        *equal++ = 0;
        if (!strcmp(line, "video_root") && Path(equal))
            strcpy(videoRoot, equal);
        else if (!strcmp(line, "return_path") && (!*equal || Path(equal)))
            strcpy(returnPath, equal);
        else
            for (int i = 0; i < Count; ++i)
                if (!strcmp(line, options[i].key))
                {
                    char *end;
                    long number = strtol(equal, &end, 10);
                    if (*equal && !*end && number >= options[i].minimum && number <= options[i].maximum)
                        values[i] = (int)number;
                }
    }
    if (values[Seek] != 5 && values[Seek] != 10 && values[Seek] != 30)
        values[Seek] = 10;
}
void Load()
{
    Defaults(Storage::Drive());
    char path[96];
    snprintf(path, sizeof(path), "%s/data/fastvideods/settings.ini", Storage::Drive());
    char *text = Storage::ReadText(path, 8192);
    if (!text)
    {
        strcat(path, ".bak");
        text = Storage::ReadText(path, 8192);
    }
    if (text)
    {
        Parse(text);
        free(text);
    }
}
bool Save()
{
    char text[4096];
    size_t used =
        snprintf(text, sizeof(text), "version=1\nvideo_root=%s\nreturn_path=%s\n", videoRoot, returnPath);
    for (int i = 0; i < Count; ++i)
    {
        int value = values[i];
        if (videoActive && values[PerVideo])
            for (unsigned j = 0; j < 3; ++j)
                if (i == playbackKeys[j])
                    value = playbackDefaults[j];
        used += snprintf(text + used, sizeof(text) - used, "%s=%d\n", options[i].key, value);
    }
    return Storage::WriteState("settings.ini", text);
}
void Change(Key key, int direction)
{
    if (key == Seek)
    {
        values[key] = direction > 0 ? (values[key] == 5    ? 10
                                       : values[key] == 10 ? 30
                                                           : 5)
                                    : (values[key] == 5    ? 30
                                       : values[key] == 30 ? 10
                                                           : 5);
        return;
    }
    const Option &o = options[key];
    int next = values[key] + direction * o.step;
    if (next > o.maximum)
        next = values[key] == o.maximum ? o.minimum : o.maximum;
    if (next < o.minimum)
        next = values[key] == o.minimum ? o.maximum : o.minimum;
    values[key] = next;
}
void ValueText(Key key, char *out, size_t size)
{
    int v = values[key];
    const char *label = 0;
    if (key == Language)
        label = v ? "Francais" : "English";
    else if (key == Scale)
        label = v ? Text("Stretch", "Etirer") : Text("Fit", "Adapter");
    else if (key == Screen)
        label = v ? Text("Bottom", "Bas") : Text("Top", "Haut");
    else if (key == Shuffle)
        label = v == 2 ? Text("Library", "Bibliotheque") : v ? Text("Folder", "Dossier") : Text("Off", "Non");
    else if (key == EndAction)
    {
        const char *en[] = {"Stop", "Next", "Repeat", "Return"};
        const char *fr[] = {"Stop", "Suivant", "Boucle", "Retour"};
        label = values[Language] ? fr[v] : en[v];
    }
    else if (options[key].maximum == 1)
        label = v ? Text("On", "Oui") : Text("Off", "Non");
    if (label)
        snprintf(out, size, "%s", label);
    else
        snprintf(out, size, "%d", v);
}
} // namespace Preferences
