#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "fastVideo.h"

/* Limits describe the current decoder and its fixed buffers, not a new format. */
#define FV_MAX_VIDEO_BYTES (64u * 1024u - 4u)
#define FV_MAX_AUDIO_BLOCKS 64u
static inline bool fv_validHeader(const fv_header_t *h, uint64_t fileSize)
{
    return h && h->signature == FV_SIGNATURE && h->width == 256 &&
        h->height >= 8 && h->height <= 192 && !(h->height & 7) &&
        h->fpsNum && h->fpsDen && h->fpsNum <= 1000000 && h->fpsDen <= 1000000 &&
        (uint64_t)h->fpsNum >= h->fpsDen && (uint64_t)h->fpsNum <= 60ull * h->fpsDen &&
        h->audioRate == FV_AUDIO_RATE && h->audioChannels == 2 &&
        h->nrFrames && h->nrFrames < 0x0fffffffu && h->nrKeyFrames &&
        h->nrKeyFrames <= h->nrFrames &&
        sizeof(*h) + (uint64_t)h->nrKeyFrames * sizeof(fv_keyframe_t) + 4 <= fileSize;
}
static inline bool fv_validRecord(uint32_t packed, uint64_t remaining)
{
    unsigned video = packed & 0x1ffff, audio = packed >> 17;
    return video >= 4 && video <= FV_MAX_VIDEO_BYTES && !(video & 1) &&
        audio <= FV_MAX_AUDIO_BLOCKS &&
        (uint64_t)video + (uint64_t)audio * 2 * FV_AUDIO_FRAME_SIZE <= remaining;
}
static inline bool fv_validKey(const fv_header_t *h, const fv_keyframe_t *key,
                                const fv_keyframe_t *previous, uint64_t fileSize)
{
    return key->frame < h->nrFrames &&
        key->offset >= sizeof(*h) + (uint64_t)h->nrKeyFrames * sizeof(*key) &&
        (uint64_t)key->offset + 4 <= fileSize &&
        (previous ? key->frame > previous->frame && key->offset > previous->offset : key->frame == 0);
}
