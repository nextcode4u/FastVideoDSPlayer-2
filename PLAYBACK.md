# Playback enhancements

This branch extends Mathos42's FastVideoDSPlayer 2. The original video format and
encoder remain unchanged. Build instructions are in [BUILDING.md](BUILDING.md).

## Starting and controls

Launch the player by itself to browse, or pass a local `.fv`, `.m3u` or `.m3u8`
path as `argv[1]` from a compatible launcher. The default video folder is
`/media/videos`; if it does not exist, browsing starts at the card root.

| Control | During playback |
| --- | --- |
| A / centre touch button | Pause or resume |
| B / back arrow | Return to the video browser |
| Left / Right | Jump 5, 10 or 30 seconds, aligned to an encoded keyframe |
| Touch timeline | Preview a destination while dragging; seek on release |
| L / Y; R / X; Previous/Next touch buttons | Previous; next video |
| START / Options touch button | Open options; START or B closes them |
| Repeat touch button | Toggle repeat |
| SELECT | Cycle shuffle |
| Up + START | Swap video and controls screens |
| Down + START | Toggle Fit / Stretch |
| Close lid | Pause; reopening leaves playback paused until fresh input |

The playback screen groups Repeat, Previous, Play/Pause, Next and Options below
the timeline. The header shows the title, playback state, picture mode and volume.
Subtitles have their own area; brief status messages use the header instead of
covering the subtitles. Long titles scroll within the header's bounds.
Options are grouped into Playback, Picture, Sound and Library pages. Use L/R or
the on-screen page buttons; bookmarks, recent videos and the queue are in Library.

In the browser, A opens, B goes up, L/R moves by a screenful, START opens options
and SELECT opens recent videos. Options use Up/Down to select, Left/Right to
change and L/R to change pages. B, START or the Back button saves and goes back.
Touch selects a row; tap the selected row again to activate it. English is the default; French remains
available in Language.

**Fit** preserves the encoded 256-pixel-wide image and centres shorter images.
**Stretch** fills 256×192 without cropping. Its vertical stretch changes the
aspect ratio deliberately. Both use the display hardware rather than resizing
video frames in software. Picture changes also work while paused.

## Options and state

Options include screen choice, repeat/shuffle, seek interval, backlight timeout
(0 disables dimming), volume, mute, mono, sound delay (±500 ms), subtitle delay
(±5 seconds), subtitles, diagnostics and resume prompts. End behavior can be
Stop, Next, Repeat or Return to launcher. Reaching the last playlist entry stops
unless repeat is enabled. Previous/next inside a playlist follows its order.

The browser's **Use this as video folder** action saves the current directory.
**Browse SD card** permits choosing another directory. Library shuffle searches
that configured folder only, on demand. B cancels between directory pages. The
index is bounded to 600 titles, 48 KiB of paths and 256 directories; it is cached
for the session. Ordinary browsing pages through larger folders independently
of that index. Names sort naturally: Episode 2 precedes Episode 10.

Settings and history live in `/data/fastvideods/settings.ini` and `history.ini`.
Writes go through the same ARM7 filesystem that reads videos. Each replacement
is written and closed first; the preceding file is retained as `.bak`. A failed
read falls back to that backup. This is not a guarantee against power loss
while the FAT itself is being changed.

Resume position is saved when pausing, seeking while paused, opening options or
leaving a video. Playback does not continuously write checkpoints. The last 32
videos are remembered, with one bookmark each. A changed frame count invalidates
an older position. Finished videos restart from the beginning. **Remember per
video** stores picture mode and audio/subtitle delays with the title while
preserving the general defaults for other videos.

## Playlists and subtitles

A playlist is a UTF-8 text file with one local `.fv` path per line. Relative paths
resolve beside the playlist; absolute `sd:/` and `fat:/` paths are supported.
Comments beginning with `#` are ignored. Network URLs, nested playlists and
non-video extensions are skipped. The queue holds at most 256 entries and paths
must fit in 255 UTF-8 bytes. Files still need to exist and pass the video checks.

For subtitles, place `Film.srt` beside `Film.fv`. Supported SRT timestamps use
`hh:mm:ss,mmm --> hh:mm:ss,mmm`; cues must be in chronological order. The player
loads at most 256 KiB and 2,048 cues. Text appears on the controls screen with
word wrapping; it does not cover the video. HTML styling is stripped. Text display
transliterates common Latin accents and substitutes unsupported
characters; UTF-8 input support does not imply full-script font coverage.

## Returning to a launcher

Back to videos and Return to launcher are separate actions. Return first uses
a valid legacy libnds return stub supplied by the launching application.

On a DSi with Unlaunch, a missing stub can use an explicit fallback in
`settings.ini`, for example:

```ini
return_path=sd:/boot.nds
```

Leave it empty to disable the fallback. The target must already exist. The
player does not install Unlaunch, modify NAND or guess which launcher to use.
An unavailable return route leaves a message and returns to browsing.

## Timing and limits

Audio playback position is measured from cascaded hardware timers. Video timing
makes bounded one-display-interval corrections against that clock. Pausing
preserves queued samples; an underrun silences the drained ring before recovery.
Diagnostics show late video frames, estimated clock difference in milliseconds,
queued audio blocks and underruns. Those counters are not a measurement of
speaker-to-LCD latency. Sound delay is an optional correction for an encode,
not a substitute for the automatic timing adjustment.

Headers, keyframe tables, record sizes, audio indices and short reads are checked
before use. IPC waits have a deadline. If a processor stops replying, the player
shows a fatal error rather than reusing shared buffers that might still be in
use. These checks do not turn the inherited assembly video decoder into a
sandbox for arbitrary damaged bitstreams.

Host tests and emulator checks cover the changes; physical DS/DSi checks still
need to establish long-duration A/V synchronization, slow-card recovery, speaker
quality, real lid sleep and launcher-specific return compatibility. Test those
with a timed flash-and-beep clip, repeated paused seeks and a known working
launcher before treating this branch as a hardware-validated release.
