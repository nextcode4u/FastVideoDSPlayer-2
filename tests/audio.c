#include <assert.h>
#include "../common/fvAudioMath.h"
int main(void)
{
    for (unsigned offset = 0; offset < 25; ++offset) {
        int16_t ring[24];
        for (int i = 0; i < 24; ++i) ring[i] = i;
        fv_rotateSamples(ring, 24, offset);
        for (int i = 0; i < 24; ++i) assert(ring[i] == (int)((i + offset) % 24));
    }
    uint16_t previous = 65534;
    assert(fv_extendBlocks(65534, &previous, 1) == 65537 && previous == 1);
    assert(fv_syncAdjustment(45, 16) == 1);
    assert(fv_syncAdjustment(-45, 16) == -1);
    assert(fv_syncAdjustment(10, 16) == 0);
}
