#pragma once

#include "util_math.c"

// Pure calculations shared by the on-chain handlers and BPF calculation tests.

// If start_price > 100,000 SOL, rounding errors could be significant.
static uint64_t compute_price(uint64_t total_seconds, uint64_t start_price, uint64_t end_price,
                              uint64_t seconds_elapsed)
{
    // Once the elapsed seconds hits the total seconds, the result is always end_price
    if (seconds_elapsed >= total_seconds) {
        return end_price;
    }

    uint64_t delta = start_price - end_price;

    // This is a curve based on the formula: y = (1 / (100x + 1)) - (1 / 101)
    // To avoid rounding errors in math, work with lamports / 1000
    delta /= 1000ul;
    end_price /= 1000ul;

    // Keep track of overflow, and if it occurs, use the end price
    bool overflow = false;

    // ac = delta * 101ul (cannot overflow since delta was already divided by 1000)
    uint64_t ac = delta * 101ul;

    // ab = ((100ul * delta * seconds_elapsed) / total_seconds) + delta (100 * delta cannot overflow)
    uint64_t ab = checked_add((checked_multiply(100ul * delta, seconds_elapsed, &overflow) / total_seconds),
                              delta,
                              &overflow);

    // bc = ((100ul * 101ul * seconds_elapsed) / total-seconds) + 101ul
    uint64_t bc = checked_add(checked_multiply(100ul * 101ul, seconds_elapsed, &overflow) / total_seconds,
                              101ul,
                              &overflow);

    // price = (end_price + ((ac - ab) / bc)) * 1000ul
    uint64_t price = checked_multiply(checked_add(end_price, ((ac - ab) / bc), &overflow),
                                      1000ul,
                                      &overflow);

    // Overflow would only occur with impossibly huge seconds_elapsed or end_price
    if (overflow) {
        price = end_price;
    }

    return price;
}

static uint64_t compute_minimum_bid(uint64_t auction_duration, uint64_t initial_minimum_bid, uint64_t current_max_bid,
                                    uint64_t seconds_elapsed)
{
    // If the maximum possible bid has been achieved, return 0
    if (current_max_bid == UINT64_MAX) {
        return 0;
    }

    // If there have been no bids yet, then use the initial minimum.  Only once the first bid is cast, does the
    // minimum bid increment come into play.
    if (current_max_bid < initial_minimum_bid) {
        return initial_minimum_bid;
    }

    // Sanitize the seconds elapsed
    if (seconds_elapsed >= auction_duration) {
        seconds_elapsed = (auction_duration - 1);
    }

    // This is a curve based on the formula: y = p * ((1 / (101 - (100 * (a / b)))) + 1.01)
    // Where a is seconds_elapsed, b is auction_duration, and p is current_max_bid.  This is a curve that goes
    // from a multiple of 1.02 of the current_max_bid at time 0, up to 2.01x the current_max_bid at the end
    // of the time range.
    uint64_t a = seconds_elapsed;
    uint64_t b = auction_duration;
    uint64_t p = current_max_bid;

    // Keep track of whether any of the math for computing the minimum bid overflows
    bool overflow = false;

    // result = (p * (((1000 * b) / ((b + (b / 100)) - a)) + 101000)) / 100000
    // The term involving b and a cannot overflow since b was originally a uint32_t value; and a is less than b.
    uint64_t result = checked_multiply(p, ((1000 * b) / ((b + (b / 100)) - a)) + 101000, &overflow) / 100000;

    // Check for overflow
    if (overflow) {
        // Overflow has occurred.  This means that the formula can't be used to compute the maximum next bid because
        // the numbers are too large.  This would only happen with extremely large bids, millions of dollars' worth.
        // But to be safe, in this case, instead of computing an invalid minimum next bid, just use 1/8 more than the
        // previous bid.
        overflow = false;
        result = checked_add(current_max_bid, (current_max_bid >> 3), &overflow);

        // If this also overflowed, then use the maximum possible bid.
        if (overflow) {
            result = UINT64_MAX;
        }
    }

    return result;
}

// Scale whole Ki earnings by effective stake / actual stake, returning deciKi.
// For x SOL and K = 500 SOL, effective(x) = ((K * x) * (K + x)) / ((K * K) + (K * x) + (x * x)).
// Cancelling x gives the multiplier (K * (K + x)) / ((K * K) + (K * x) + (x * x)), also defined at zero.
static uint64_t compute_ki_harvest(uint64_t harvest_amount, uint64_t stake_lamports, bool *overflow)
{
    // Round the curve input down by less than one SOL for ordinary balances.
    uint64_t stake_sol = stake_lamports / 1000000000ul;
    uint64_t ceiling_sol = 500ul;

    // Above one billion SOL, use ten-SOL units for BOTH x and K. This leaves the ratio unchanged apart from rounding
    // the input down by less than ten SOL. Even UINT64_MAX lamports then gives x <= 1,844,674,407 units.
    if (stake_sol > 1000000000ul) {
        stake_sol /= 10;
        ceiling_sol /= 10;
    }

    // In either unit system, the numerator is below 501 billion and the denominator is below 3.5 quintillion. Even
    // three times the denominator fits in uint64_t, which also bounds the division intermediates below.
    uint64_t numerator = ceiling_sol * (ceiling_sol + stake_sol);
    uint64_t denominator = numerator + (stake_sol * stake_sol);

    // Convert to the token's one-decimal-place units before reducing earnings.
    uint64_t deciki = checked_multiply(harvest_amount, 10, overflow);
    if (*overflow) {
        return 0;
    }

    // Compute floor((deciki * numerator) / denominator) without forming the product, which could overflow even though
    // the final answer fits in uint64_t.  Process one input bit at a time. The remainder stays below denominator;
    // each step is below (3 * denominator) and requires at most two subtractions.  Because numerator <= denominator,
    // the final result fits in uint64_t.
    uint64_t remainder = 0;
    uint64_t result = 0;
    for (unsigned bit = 64; bit > 0; --bit) {
        remainder = (remainder << 1) + (((deciki >> (bit - 1)) & 1) ? numerator : 0);
        uint64_t digit = 0;
        if (remainder >= denominator) {
            remainder -= denominator;
            digit = 1;
            if (remainder >= denominator) {
                remainder -= denominator;
                digit = 2;
            }
        }
        result = (result << 1) + digit;
    }
    return result;
}

static uint64_t compute_commission(uint64_t current_stake, uint64_t baseline, uint16_t commission)
{
    return ((current_stake - baseline) * commission) / 0xFFFFul;
}
