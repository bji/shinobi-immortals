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

// Preserves the existing integer rounding and overflow behavior.
static uint64_t compute_ki_reduction(uint64_t harvest_amount, bool *overflow)
{
    // Reduce the amount to harvest, to discourage very large Ki harvests per entry.  The amount to harvest is the
    // function:
    // x = (x - (x^4/106666^3))
    // To avoid rounding errors, this is refactored.
    uint64_t f = checked_multiply(harvest_amount, harvest_amount, overflow) / 106666ul;

    // harvest_amount * 10666ul cannot overflow since the original value was divided by LAMPORTS_PER_SOL
    harvest_amount = ((harvest_amount * 106666ul) - checked_multiply(f, f, overflow)) / 1066666ul;

    // Because Ki tokens have a decimal place count of 1, which is necessary to comply with metaplex metadata
    // standards for fungible tokens, multiply the actual number of Ki by 10.
    harvest_amount = checked_multiply(harvest_amount, 10, overflow);

    return harvest_amount;
}

static uint64_t compute_commission(uint64_t current_stake, uint64_t baseline, uint16_t commission)
{
    return ((current_stake - baseline) * commission) / 0xFFFFul;
}
