#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p tmp
flags=(-Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g)
for test in display validation audio; do
    cc -std=c99 "${flags[@]}" "tests/$test.c" -o "tmp/$test-test"
    "tmp/$test-test"
done
c++ -std=c++11 "${flags[@]}" -Wno-reorder -Itests/host tests/controller.cpp \
    arm9/source/gui/PlayerController.cpp arm9/source/gui/core/InputRepeater.cpp \
    arm9/source/Preferences.cpp -o tmp/controller-test
./tmp/controller-test
c++ -std=c++11 "${flags[@]}" tests/media.cpp arm9/source/MediaText.cpp \
    arm9/source/Preferences.cpp arm9/source/LibraryState.cpp -o tmp/media-test
./tmp/media-test
# A disposable image exercises the same FatFs code used on the console.
image=$(mktemp tmp/fatfs-XXXXXX.img)
trap 'rm -f "$image"' EXIT
truncate -s 64M "$image"
mkfs.fat -F 16 "$image" >/dev/null
cc -std=c99 "${flags[@]}" -Wno-unused-function -Dsniprintf=snprintf -Itests/hostc \
    tests/storage.c arm7/source/fileService.c arm7/source/directoryService.c \
    arm7/source/fat/ff.c arm7/source/fat/ffunicode.c -o tmp/storage-test
./tmp/storage-test "$image"
printf 'Host checks passed: display, validation, audio, controller, text/state, FAT storage/paging.\n'
