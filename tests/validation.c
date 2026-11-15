#include <stdint.h>
#include <assert.h>
typedef uint32_t u32;
typedef uint16_t u16;
#include "../common/fvValidation.h"
int main(void)
{
    fv_header_t h = { FV_SIGNATURE, 256, 144, 30, 1, FV_AUDIO_RATE, 2, 300, 2 };
    assert(fv_validHeader(&h, 100000));
    h.fpsDen = 0; assert(!fv_validHeader(&h, 100000)); h.fpsDen = 1;
    h.nrKeyFrames = UINT32_MAX; assert(!fv_validHeader(&h, 100000)); h.nrKeyFrames = 2;
    h.width = 257; assert(!fv_validHeader(&h, 100000)); h.width = 256;
    h.audioRate = 44100; assert(!fv_validHeader(&h, 100000)); h.audioRate = FV_AUDIO_RATE;
    assert(!fv_validHeader(&h, 30));
    assert(fv_validRecord(100 | (6u << 17), 100 + 6 * 264));
    assert(!fv_validRecord(100 | (6u << 17), 100 + 6 * 264 - 1));
    assert(!fv_validRecord(65536, 1000000));
    assert(!fv_validRecord(100 | (65u << 17), 1000000));
    assert(!fv_validRecord(101, 1000000));
    fv_keyframe_t a = {0, 44}, b = {100, 1000};
    assert(fv_validKey(&h, &a, 0, 100000));
    assert(fv_validKey(&h, &b, &a, 100000));
    assert(!fv_validKey(&h, &a, &b, 100000));
    b.offset = 100000; assert(!fv_validKey(&h, &b, &a, 100000));
}
