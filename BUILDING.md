# Building FastVideoDSPlayer 2

The player uses the classic libnds FIFO API. The CI image is pinned to
**devkitARM r65-1, libnds 1.8.3-1 and ndstool 2.3.1-1**. From the repository root:

```sh
docker run --rm -v "$PWD:/work" -w /work \
  devkitpro/devkitarm@sha256:a998edf6b06416b5c053edbcd879abfa22b1b88e9cd3f267f5c4ee9fec71a93a \
  make -j2
```

The result is `FastVideoDS.nds`. A native installation of the same SDK can use
`make -j2` with the usual devkitPro environment variables. Build output is ignored
by Git. Clean before switching SDK versions (`make clean`).

## Legacy interrupt-wait fix

`arm9/source/IntrWait.s` provides the libnds 1.8 ARM9 wait routines with the
missing MMIO-base initialization on their immediate-return path. The normal
Makefile includes it automatically; the installed SDK does not need modifying.
The original source notice and revision are retained in the file.

The override is enabled only for libnds 1.8. libnds 1.8.3's generated version
header still reports 1.8.0, so the guard checks major/minor. With a newer SDK the
file supplies no replacement symbols. This guard alone does not port the rest
of the player to libnds 2 or establish compatibility with that SDK.

Keep Gericom's original-player credit, Mathos42's fork credit and their license
explanation in `readme.md`. The root zlib license covers Mathos42's additions;
it does not relicense the original player. The wait routine retains its own
notice, and FatFs retains the notice in `arm7/source/fat/ff.c`.

## Checks

On Linux with a C/C++ compiler and `dosfstools` installed:

```sh
bash scripts/test-host.sh
```

The script builds six AddressSanitizer/UndefinedBehaviorSanitizer test programs:

- display transforms for every supported height;
- container, keyframe and record validation;
- audio-ring rotation, timer rollover and synchronization corrections;
- the actual controller with hardware substitutes (lid priority, options/touch controls, screen shortcuts and paused seeks);
- playlists, UTF-8/SRT handling, settings bounds, history and per-video defaults;
- the actual FatFs state service and directory pager on a temporary FAT image,
  including an injected write failure and forward/backward pages across 140 files.

All generated images and executables stay under ignored `tmp/`. No ROMs, firmware
or private media are test dependencies.

## Source organization

`common/` holds wire formats and portable validation/math. The ARM7 owns SD/FAT
access, audio queues and playback clocks. `fileService` limits writes to the two
state files; `directoryService` returns bounded naturally sorted pages. The ARM9
`Storage`, `Preferences`, `LibraryState` and `MediaText` modules keep persistence
and parsing outside the player UI. `OptionsMenu` is shared by browser and player.

`appStack.s` gives foreground ARM9 code 32 KiB in main RAM. Interrupt stacks and
hot decoder tables stay in DTCM; large libc/UI call chains must not overwrite
them. Shared IPC data stays in main RAM and is flushed/invalidated at handoff.
Audio buffers are also in main RAM to leave adequate ARM7 IWRAM stack space.
Text reads shrink to their actual length once the ARM7 has acknowledged them.

The return helper uses the pinned libnds 1.8 transfer-region ABI (64-bit `time_t`,
return-stub pointer at +24). Recheck that ABI when changing SDKs. The optional
Unlaunch route follows the published [autoload protocol](https://problemkaputt.de/gbatek.htm#dsiautoloadonwarmboot).

## Console validation

Use both an ordinary clip and an original 256×144 timed flash-and-beep clip.
Check Fit/Stretch preserves the top and bottom edges, sound stays aligned over
time, paused seeks remain silent and paused, and lid reopening requires fresh
input. Repeat seeking, video changes, queue completion and screen swapping.
Check state survives a restart, an invalid video returns a useful error, and
both the caller's return stub and an explicitly configured Unlaunch fallback
return to the expected application. Emulator results do not establish physical
SD timing, sound quality or all launcher return routes.
