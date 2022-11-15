#pragma once


/**
 * Multiplies [x] by [y] and returns the result.  The result may have overflowed, and if so, the return value
 * [overflowed] will be set to true; otherwise the return value [overflowed] will be unchanged.
 **/
static uint64_t checked_add(const uint64_t x, const uint64_t y, bool *overflowed)
{
    uint64_t result = x + y;

    if ((result < x) || (result < y)) {
        *overflowed = true;
    }

    return result;
}


/**
 * Multiplies [x] by [y] and returns the result.  The result may have overflowed, and if so, the return value
 * [overflowed] will be set to true; otherwise the return value [overflowed] will be unchanged.
 **/
static uint64_t checked_multiply(const uint64_t x, const uint64_t y, bool *overflowed)
{
    // Implemented using the schoolbook multiplication algorithm
    const uint64_t x0 = (uint32_t) x;
    const uint64_t x1 = x >> 32;
    const uint64_t y0 = (uint32_t) y;
    const uint64_t y1 = y >> 32;
    const uint64_t p11 = x1 * y1;
    const uint64_t p01 = x0 * y1;
    const uint64_t p10 = x1 * y0;
    const uint64_t p00 = x0 * y0;

    const uint64_t middle = p10 + (p00 >> 32) + ((uint32_t) p01);

    if (p11 + (middle >> 32) + (p01 >> 32)) {
        *overflowed = true;
    }

    return (middle << 32) | ((uint32_t) p00);
}
