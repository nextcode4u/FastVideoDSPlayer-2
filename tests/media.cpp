#include <cassert>
#include <cstring>
#include <cstdlib>
#include <string>
#include "../arm9/source/MediaText.h"
#include "../arm9/source/LibraryState.h"
#include "../arm9/source/Preferences.h"
#include "../arm9/source/Storage.h"
#include "../common/fvNames.h"
static std::string savedState, inputState;
namespace Storage
{
const char *Drive()
{
    return "sd:";
}
char *ReadText(const char *, size_t, int *)
{
    return inputState.empty() ? 0 : strdup(inputState.c_str());
}
bool WriteState(const char *, const char *text)
{
    savedState = text;
    return true;
}
} // namespace Storage
int main()
{
    assert(fv_naturalCompare("Episode 2.fv", "Episode 10.fv") < 0);
    assert(fv_naturalCompare("Episode 0002.fv", "Episode 10.fv") < 0);
    assert(fv_naturalCompare("999999999999999999", "1000000000000000000") < 0);
    char path[256];
    assert(MediaText::ResolvePath("sd:/media/list.m3u", "../films/One.fv", path, sizeof(path)));
    assert(!strcmp(path, "sd:/films/One.fv"));
    assert(!MediaText::ResolvePath("sd:/list.m3u", "../escape.fv", path, sizeof(path)));
    assert(!MediaText::ResolvePath("sd:/list.m3u", "https://example/video.fv", path, sizeof(path)));
    std::string longName(300, 'a');
    assert(!MediaText::ResolvePath("sd:/list.m3u", longName.c_str(), path, sizeof(path)));
    char list[] = "#EXTM3U\nEpisode 2.fv\r\n../One.fv\nhttps://x.fv\nother.mp4\n";
    char entries[4][256];
    size_t skipped;
    assert(MediaText::Playlist(list, "sd:/media/list.m3u", entries, 4, skipped) == 2 && skipped == 2);
    char srt[] = "\xef\xbb\xbf"
                 "1\r\n00:00:01,000 --> 00:00:02,500\r\nHello <i>world</i>\r\n\r\n2\n00:00:04,000 --> "
                 "00:00:05,000\nNext\n";
    MediaText::Cue cues[8];
    size_t count = MediaText::Subtitles(srt, cues, 8, skipped);
    assert(count == 2 && !skipped);
    assert(MediaText::ActiveCue(cues, count, 999) == -1);
    assert(MediaText::ActiveCue(cues, count, 1000) == 0);
    assert(MediaText::ActiveCue(cues, count, 2500) == -1);
    assert(MediaText::ActiveCue(cues, count, 4500) == 1);
    char display[64];
    MediaText::DisplayText(srt + cues[0].text, cues[0].length, display, sizeof(display));
    assert(!strcmp(display, "Hello world "));
    char invalid[] = "00:00:99999999999999,000 --> 00:00:05,000\nbad";
    assert(MediaText::Subtitles(invalid, cues, 8, skipped) == 0);
    const char shortUtf8[] = {(char)0xf0, (char)0x9f, 0};
    MediaText::DisplayText(shortUtf8, 2, display, sizeof(display));
    assert(strlen(display) <= 2);
    Preferences::Defaults("sd:");
    char settings[] = "volume=99999999999999999999999999\nlanguage=1\nseek_seconds=7\naudio_delay_ms=-"
                      "75\nvideo_root=sd:/films\n";
    Preferences::Parse(settings);
    assert(Preferences::values[Preferences::Volume] == 127);
    assert(Preferences::values[Preferences::Language] == 1);
    assert(Preferences::values[Preferences::Seek] == 10);
    assert(Preferences::values[Preferences::AudioDelay] == -75);
    assert(!strcmp(Preferences::videoRoot, "sd:/films"));
    Preferences::values[Preferences::PerVideo] = 1;
    Preferences::BeginVideo(1, 100, 250, true);
    assert(Preferences::values[Preferences::Scale] == 1 &&
           Preferences::values[Preferences::AudioDelay] == 100);
    assert(Preferences::Save() && savedState.find("audio_delay_ms=-75\n") != std::string::npos);
    Preferences::EndVideo();
    assert(Preferences::values[Preferences::Scale] == 0 &&
           Preferences::values[Preferences::AudioDelay] == -75);
    inputState = "10,5,300,1,25,-250\tsd:/One.fv\n999999999999999999999,0,300,0,0,0\tsd:/bad.fv\n";
    LibraryState::Load();
    assert(LibraryState::recentCount == 1);
    assert(LibraryState::Find("sd:/One.fv")->bookmark == 5);
    LibraryState::Remember("sd:/One.fv", 20, 300, false);
    assert(LibraryState::recentCount == 1 && LibraryState::Find("sd:/One.fv")->bookmark == 5);
    LibraryState::Remember("sd:/Two.fv", 15, 300, false);
    assert(LibraryState::recentCount == 2 && !strcmp(LibraryState::recent[0].path, "sd:/Two.fv"));
    assert(LibraryState::Save());
    inputState = savedState;
    LibraryState::Load();
    assert(LibraryState::recentCount == 2 && LibraryState::Find("sd:/One.fv")->frame == 20);
    LibraryState::Remember("sd:/One.fv", 299, 300, true);
    assert(LibraryState::recent[0].frame == 0 && LibraryState::recent[0].bookmark == 5);
    LibraryState::Remember("sd:/One.fv", 0, 400, false);
    assert(LibraryState::recent[0].bookmark == 0);
    for (unsigned seed = 1; seed < 3000; ++seed)
    {
        char malformed[129];
        unsigned n = seed;
        for (unsigned i = 0; i < 128; ++i)
        {
            n = n * 1664525 + 1013904223;
            malformed[i] = (n % 95) + 32;
        }
        malformed[128] = 0;
        MediaText::Subtitles(malformed, cues, 8, skipped);
        MediaText::DisplayText(malformed, 128, display, sizeof(display));
    }
}
