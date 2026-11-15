#pragma once
#include <stdint.h>
#include <stddef.h>

/* In-place rotation keeps unplayed PCM across a pause without another ring. */
static inline void fv_reverseSamples(int16_t *data, unsigned count)
{
    for (unsigned i = 0; i < count / 2; ++i) {
        int16_t value = data[i]; data[i] = data[count - i - 1]; data[count - i - 1] = value;
    }
}
static inline void fv_rotateSamples(int16_t *data, unsigned count, unsigned offset)
{
    if (!count || !(offset %= count)) return;
    fv_reverseSamples(data, offset);
    fv_reverseSamples(data + offset, count - offset);
    fv_reverseSamples(data, count);
}
static inline uint32_t fv_extendBlocks(uint32_t total, uint16_t *previous, uint16_t now)
{
    total += (uint16_t)(now - *previous);
    *previous = now;
    return total;
}
/* Positive delay means sound later (video leads). Correct gradually by at most
   one display interval; never skip predictive-frame decoding. */
static inline int fv_syncAdjustment(int errorMs, int intervalMs)
{
    if (errorMs > intervalMs) return 1;
    if (errorMs < -intervalMs) return -1;
    return 0;
}
