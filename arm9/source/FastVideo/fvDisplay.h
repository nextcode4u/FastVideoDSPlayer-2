#pragma once
#include <stdbool.h>
#include <stdint.h>

// The decoder produces 256-pixel-wide images. Scaling changes only the
// displayed bitmap; decoded reference frames and audio timing stay unchanged.
typedef enum
{
    FV_SCALE_FIT = 0,
    FV_SCALE_STRETCH
} fv_scale_mode_t;

typedef struct
{
    int16_t verticalStep; // inverse scale in 8.8 fixed point
    int32_t sourceY;      // source coordinate at the first display line
} fv_display_transform_t;

static inline bool fv_displayTransform(unsigned height, fv_scale_mode_t mode,
                                       fv_display_transform_t* transform)
{
    if (!transform || height < 8 || height > 192 || height % 8 != 0)
        return false;
    if (mode == FV_SCALE_FIT)
    {
        transform->verticalStep = 256;
        transform->sourceY = -(int32_t)((192 - height) / 2) * 256;
    }
    else if (mode == FV_SCALE_STRETCH)
    {
        transform->verticalStep = (height * 256) / 192;
        // Center the samples within the image. With the rounded-down step,
        // both edge rows remain visible and no sample reaches past the image.
        transform->sourceY = ((int32_t)height * 256 - 191 * transform->verticalStep) / 2;
    }
    else
        return false;
    return true;
}
