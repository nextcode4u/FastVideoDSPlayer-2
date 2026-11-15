#include <assert.h>
#include <stddef.h>
#include "../arm9/source/FastVideo/fvDisplay.h"

int main(void)
{
    fv_display_transform_t t;
    for (unsigned height = 0; height <= 256; height++)
    {
        bool valid = height >= 8 && height <= 192 && height % 8 == 0;
        assert(fv_displayTransform(height, FV_SCALE_FIT, &t) == valid);
        if (!valid)
            continue;

        // Fit displays each encoded row once, centered with equal black bars.
        int top = (192 - height) / 2;
        assert(t.sourceY + top * t.verticalStep == 0);
        assert(t.sourceY + (top + (int)height - 1) * t.verticalStep == (int)(height - 1) * 256);

        assert(fv_displayTransform(height, FV_SCALE_STRETCH, &t));
        bool visible[192] = { false };
        for (int y = 0; y < 192; y++)
        {
            int sample = (t.sourceY + y * t.verticalStep) / 256;
            assert(sample >= 0 && sample < (int)height);
            visible[sample] = true;
        }
        // No source row is cropped, including the first and last rows.
        for (unsigned y = 0; y < height; y++)
            assert(visible[y]);
    }
    assert(!fv_displayTransform(144, (fv_scale_mode_t)2, &t));
    assert(!fv_displayTransform(144, FV_SCALE_FIT, NULL));
    assert(fv_displayTransform(144, FV_SCALE_FIT, &t));
    assert(t.sourceY == -24 * 256);
    assert(fv_displayTransform(192, FV_SCALE_STRETCH, &t));
    for (int y = 0; y < 192; y++)
        assert((t.sourceY + y * t.verticalStep) / 256 == y);
    return 0;
}
