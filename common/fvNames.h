#pragma once
#include <ctype.h>
#include <string.h>
/* Numeric runs compare by magnitude without integer conversion or overflow. */
static inline int fv_naturalCompare(const char *a, const char *b)
{
    const char *originalA = a, *originalB = b;
    while (*a && *b)
    {
        if (*a >= '0' && *a <= '9' && *b >= '0' && *b <= '9')
        {
            const char *ae = a, *be = b;
            while (*ae == '0')
                ++ae;
            while (*be == '0')
                ++be;
            const char *az = ae, *bz = be;
            while (*az >= '0' && *az <= '9')
                ++az;
            while (*bz >= '0' && *bz <= '9')
                ++bz;
            if (az - ae != bz - be)
                return az - ae < bz - be ? -1 : 1;
            int cmp = memcmp(ae, be, az - ae);
            if (cmp)
                return cmp;
            a = az;
            b = bz;
        }
        else
        {
            int ac = tolower((unsigned char)*a++), bc = tolower((unsigned char)*b++);
            if (ac != bc)
                return ac - bc;
        }
    }
    if (*a || *b)
        return (unsigned char)*a - (unsigned char)*b;
    return strcmp(originalA, originalB); // stable total order, including leading zeroes
}
