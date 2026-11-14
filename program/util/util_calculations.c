#pragma once

#include "util_math.c"

// Pure calculations shared by the on-chain handlers and BPF calculation tests.

// Level credits are earned on a curve that resets at the start of each level credit window.  Windows are fixed spans
// of wall-clock time (Clock.unix_timestamp), numbered from the Unix epoch, so every entry's window starts and ends
// at the same moment.  The window length is a build-time constant so that it can never be supplied or altered at
// run time.  Production builds use the default of 5 days, in seconds (note: seconds, not slots, despite also
// equalling the number of slots in a nominal Solana epoch).  Only the tests define a different (much shorter) value,
// so that window rollover can be tested in real time; the reproducible build of the published program uses the
// default.
#ifndef LEVEL_CREDIT_WINDOW_SECONDS
#define LEVEL_CREDIT_WINDOW_SECONDS (5ul * 24ul * 60ul * 60ul)
#endif

// A 365 day year, in seconds
#define SECONDS_PER_YEAR (365ul * 24ul * 60ul * 60ul)

// The number of windows per year, N (73 for the default 5 day window), must be at least 3:
// - N must be non-zero, since every level credit calculation depends upon it.
// - With 4*N >= 10, the credit calculation's check that 4*N*(window Ki total) does not overflow also guarantees
//   that converting the applied whole Ki to deciKi (x10) for burning cannot overflow.
static_assert((LEVEL_CREDIT_WINDOW_SECONDS > 0) && ((SECONDS_PER_YEAR / LEVEL_CREDIT_WINDOW_SECONDS) >= 3),
              "LEVEL_CREDIT_WINDOW_SECONDS must be non-zero and give at least 3 level credit windows per year");
#define LEVEL_CREDIT_WINDOWS_PER_YEAR (SECONDS_PER_YEAR / LEVEL_CREDIT_WINDOW_SECONDS)

// floor((value * numerator) / denominator), with (numerator <= denominator) and (denominator > 0).  Reduce after each
// addition rather than forming a potentially overflowing product.
static uint64_t multiply_fraction(uint64_t value, uint64_t numerator, uint64_t denominator)
{
    uint64_t remainder = 0;
    uint64_t result = 0;
    for (unsigned bit = 64; bit > 0; --bit) {
        uint64_t digit = 0;
        // Double the remainder modulo denominator without overflowing.
        if (remainder >= denominator - remainder) {
            remainder -= denominator - remainder;
            digit = 1;
        }
        else {
            remainder += remainder;
        }
        if ((value >> (bit - 1)) & 1) {
            if (remainder >= denominator - numerator) {
                remainder -= denominator - numerator;
                digit++;
            }
            else {
                remainder += numerator;
            }
        }
        result = (result << 1) + digit;
    }
    return result;
}

// Returns the price at seconds_elapsed into a price curve that falls from start_price to end_price over
// total_seconds, which requires start_price >= end_price.  The curve is y = (1 / (100x + 1)) - (1 / 101), where
// x = seconds_elapsed / total_seconds; it runs from 100/101 at x = 0 down to 0 at x = 1.  The price is
// end_price + (start_price - end_price) * y, rounded down, computed in whole lamports.  So the price starts just below
// start_price (about 99% of the way from end_price to it) and falls to end_price; since the term added to end_price
// is never negative, the price is never below end_price.  Callers' prices are limited to 100,000,000 SOL
// (see admin_create_block), far below the roughly 182 million SOL at which the intermediate values below would
// overflow; if they ever did overflow, end_price is returned.
static uint64_t compute_price(uint64_t total_seconds, uint64_t start_price, uint64_t end_price,
                              uint64_t seconds_elapsed)
{
    // Once the elapsed seconds hits the total seconds, the result is always end_price
    if (seconds_elapsed >= total_seconds) {
        return end_price;
    }

    // The amount by which the price falls over the whole curve
    uint64_t delta = start_price - end_price;

    // Keep track of overflow, and if it occurs, use the end price
    bool overflow = false;

    // With x = seconds_elapsed / total_seconds, the amount added to end_price is
    //     delta * ((1 / (100x + 1)) - (1 / 101)) = (101 * delta - (100 * delta * x + delta)) / (101 * (100x + 1))
    // which is computed as (ac - ab) / bc, with each time term rounded down:

    // ac = 101 * delta
    uint64_t ac = checked_multiply(delta, 101ul, &overflow);

    // ab = ((100 * delta * seconds_elapsed) / total_seconds) + delta.
    // The product 100 * delta * seconds_elapsed can overflow for large prices and long durations (for example, a
    // 100,000 SOL start price after about 31 minutes), so it is never formed: multiply_fraction() computes the same
    // floor without it.  It requires seconds_elapsed <= total_seconds and total_seconds > 0, both guaranteed by the
    // early return above.  The result is at most 100 * delta, so ab is at most ac, and ac - ab cannot underflow.
    uint64_t ab = checked_add(multiply_fraction(checked_multiply(delta, 100ul, &overflow), seconds_elapsed,
                                                total_seconds),
                              delta, &overflow);

    // bc = ((100 * 101 * seconds_elapsed) / total_seconds) + 101, computed the same way
    uint64_t bc = checked_add(multiply_fraction(100ul * 101ul, seconds_elapsed, total_seconds), 101ul, &overflow);

    // price = end_price + ((ac - ab) / bc).  The added term is at most delta, so the sum is at most start_price.
    uint64_t price = checked_add(end_price, (ac - ab) / bc, &overflow);

    // Defensive fallback only; no price accepted by admin_create_block can overflow
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

// Scale actual stake earnings in lamports by effective stake / actual stake,
// returning effective stake earnings in lamports.
// For x SOL and K = 500 SOL, effective(x) = ((K * x) * (K + x)) / ((K * K) + (K * x) + (x * x)).
// Cancelling x gives the multiplier (K * (K + x)) / ((K * K) + (K * x) + (x * x)), also defined at zero.
static uint64_t compute_effective_stake_earnings(uint64_t earnings_lamports, uint64_t stake_lamports)
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

    // Compute floor((earnings_lamports * numerator) / denominator) without forming the product, which could overflow
    // even though the final answer fits in uint64_t.  Process one input bit at a time. The remainder stays below
    // denominator; each step is below (3 * denominator) and requires at most two subtractions.  Because numerator <=
    // denominator, the final result fits in uint64_t.
    uint64_t remainder = 0;
    uint64_t result = 0;
    for (unsigned bit = 64; bit > 0; --bit) {
        remainder = (remainder << 1) + (((earnings_lamports >> (bit - 1)) & 1) ? numerator : 0);
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

// Convert bounded effective stake earnings to whole Ki, rounding only after multiplying fractional SOL by the
// factor. The caller supplies earnings from compute_effective_stake_earnings with actual earnings <= current stake.
// The exact curve caps effective earnings below 500 SOL. Rounding its stake input adds less than one SOL (or ten SOL
// for enormous balances), so 510 SOL is a conservative bound for every valid uint64_t stake balance.
static uint64_t compute_ki_harvest(uint64_t effective_earnings_lamports, uint32_t ki_factor)
{
    // Avoid forming effective_earnings_lamports * ki_factor: that product can overflow even for 50 SOL of earnings
    // and a large uint32_t factor.  The whole-SOL product is below 510*UINT32_MAX; the fractional product is below
    // 1,000,000,000*UINT32_MAX. Both, and their sum after division, fit.
    uint64_t whole_sol = effective_earnings_lamports / 1000000000ul;
    uint64_t remaining_lamports = effective_earnings_lamports % 1000000000ul;
    return (whole_sol * ki_factor) + ((remaining_lamports * ki_factor) / 1000000000ul);
}

static uint64_t compute_commission(uint64_t current_stake, uint64_t baseline, uint16_t commission)
{
    // The program never removes stake from a stake account other than by charging commission (which lowers the
    // baseline to match), so delegated stake should never fall below the baseline.  But a future stake program change,
    // such as slashing, could reduce it.  A stake that has not grown has earned nothing, so charge no commission rather
    // than letting the unsigned subtraction below wrap to an enormous value; such a value would make every commission
    // charge (including the one made by destake) fail until rewards had restored the stake above the baseline.
    // The caller then records the current stake as the new baseline, as Ki harvesting does in the same situation.
    if (current_stake <= baseline) {
        return 0;
    }

    // Commission is the earned lamports times commission / 0xFFFF, rounded down.  The product of the earned lamports
    // and the commission can overflow (for earnings above about 281,000 SOL at the maximum commission), so it is never
    // formed: multiply_fraction() computes the same floor without it.  It requires the numerator to be no more than
    // the denominator, which holds since commission is a uint16_t and so at most 0xFFFF.  The result is at most the
    // earned lamports.
    return multiply_fraction(current_stake - baseline, commission, 0xFFFFul);
}

// Returns the level credit window to use for a contribution made at unix_timestamp, given the window most recently
// recorded for the entry.  Normally this is just unix_timestamp / window_seconds.  However, the cluster's
// Clock.unix_timestamp is a stake-weighted estimate rather than a strictly monotonic value, so in principle it could
// step backwards across a window boundary.  If the computed window were simply compared for equality with the
// recorded one, such a step would look like a new window and grant a fresh credit curve (and stepping forward again
// would grant yet another).  To prevent that, a window earlier than the recorded one is clamped to the recorded
// window, so that the contribution continues on that window's curve.  Windows can therefore only ever move forward
// for an entry.
// window_seconds is a parameter, rather than LEVEL_CREDIT_WINDOW_SECONDS being used directly, so that the unit tests
// can exercise both the production and the test window lengths; it must be non-zero.
static uint64_t compute_level_credit_window(uint64_t unix_timestamp, uint64_t most_recent_window,
                                            uint64_t window_seconds)
{
    // The window that the timestamp falls in, numbered from the Unix epoch
    uint64_t window = unix_timestamp / window_seconds;

    // Never move backwards relative to the window already recorded for the entry
    return (window < most_recent_window) ? most_recent_window : window;
}

// Cumulative level requirement in tenths of a credit. There are eight upgrades from displayed level 1 (index 0) to
// displayed level 9 (index 8).
static uint64_t compute_level_requirement(uint32_t level_1_credits, uint8_t upgrades)
{
    uint64_t cost = ((uint64_t) level_1_credits * 10);
    uint64_t total = 0;
    for (uint8_t i = 0; i < upgrades; ++i) {
        total += cost;
        cost += cost >> 1;
    }
    return total;
}

// P(k) = 6*L*k / (N*(5*B+k)), in tenths of a level credit.  L is the lifetime requirement, N windows/year, k the Ki
// contributed this window, and B the Ki produced in one window by a 500 effective SOL stake account:
//   B = ki_factor * 500 * (R / N), where R = reward_bps / 10000 is the expected annual reward rate.
// Contributing B every window earns L/N per window, i.e. levels fully in one year; the most that can be earned in a
// window, however much Ki is contributed, is 6*L/N (a minimum of N/6 windows, about two months, to level fully).
// Substituting B and multiplying through by 4*N gives an exact integer form with a single rounding:
//   P(k) = 24*L*k / (ki_factor*reward_bps + 4*N*k)
// windows_per_year is a parameter, rather than LEVEL_CREDIT_WINDOWS_PER_YEAR being used directly, so that the
// calculation tests can check the formula across a range of values.
static uint64_t compute_level_credits(uint32_t level_1_credits, uint32_t ki_factor, uint32_t reward_bps,
                                      uint64_t window_ki, uint64_t windows_per_year, bool *overflow)
{
    if (!level_1_credits || !ki_factor || !reward_bps || !windows_per_year) {
        *overflow = true;
        return 0;
    }
    // ki_factor*reward_bps is a product of two uint32_t values and so fits uint64_t
    uint64_t rate_term = (uint64_t) ki_factor * reward_bps;
    uint64_t four_n_k = checked_multiply(checked_multiply(windows_per_year, 4, overflow), window_ki, overflow);
    uint64_t denominator = checked_add(rate_term, four_n_k, overflow);
    if (*overflow) {
        return 0;
    }
    // A uint32_t first-level cost gives L < 2^42; 24*L < 2^47 also fits uint64_t.
    uint64_t twenty_four_total = 24 * compute_level_requirement(level_1_credits, 8);
    // window_ki <= 4*N*window_ki < denominator (N >= 1 and rate_term > 0), as multiply_fraction requires.
    return multiply_fraction(twenty_four_total, window_ki, denominator);
}

// Returns the smallest window Ki total k for which compute_level_credits(k) >= target_credits, i.e. the least Ki that
// the window must have received in total for its curve to reach target_credits.  Returns UINT64_MAX if no window Ki
// total can reach the target (the curve approaches, but never reaches, 6*L/N), or if the answer does not fit.
//
// With A = 24*L, C = ki_factor*reward_bps and D = 4*N, the curve is P(k) = floor(A*k / (C + D*k)).  Because the
// target T is an integer, P(k) >= T exactly when A*k >= T*(C + D*k), i.e. when k*(A - T*D) >= T*C.  So the target is
// unreachable when A <= T*D, and otherwise the answer is ceil(T*C / (A - T*D)).  T*C can exceed 64 bits, so with
// E = A - T*D it is computed as T*(C/E) + T*(C%E)/E, the second term by multiply_fraction since C%E < E.  The floor
// computed that way is then checked against compute_level_credits itself and stepped up by one if it falls short,
// so the result agrees exactly with the credits that will actually be awarded.
//
// windows_per_year is a parameter for the same reason as for compute_level_credits.
static uint64_t compute_level_credits_ki_needed(uint32_t level_1_credits, uint32_t ki_factor, uint32_t reward_bps,
                                                uint64_t target_credits, uint64_t windows_per_year)
{
    // A zero target needs no Ki at all
    if (!target_credits) {
        return 0;
    }
    // Invalid inputs earn no credits, so no amount of Ki can reach the target
    if (!level_1_credits || !ki_factor || !reward_bps || !windows_per_year) {
        return UINT64_MAX;
    }

    bool overflow = false;
    // A = 24*L fits uint64_t (see compute_level_credits); C is a product of two uint32_t values
    uint64_t a = 24 * compute_level_requirement(level_1_credits, 8);
    uint64_t c = (uint64_t) ki_factor * reward_bps;
    // T*D, where D = 4*N.  If this overflows it certainly exceeds A, so the target is unreachable.
    uint64_t td = checked_multiply(checked_multiply(windows_per_year, 4, &overflow), target_credits, &overflow);
    if (overflow || (td >= a)) {
        return UINT64_MAX;
    }
    uint64_t e = a - td;

    // floor(T*C / E) = T*(C/E) + floor(T*(C%E) / E)
    uint64_t k = checked_add(checked_multiply(target_credits, c / e, &overflow),
                             multiply_fraction(target_credits, c % e, e), &overflow);
    if (overflow) {
        return UINT64_MAX;
    }

    // The exact answer is either the floor or one more than it.  Confirm with the real credit calculation, which
    // also covers any case where the inverse above and the forward calculation might disagree.
    uint64_t credits = compute_level_credits(level_1_credits, ki_factor, reward_bps, k, windows_per_year, &overflow);
    if (!overflow && (credits >= target_credits)) {
        return k;
    }
    if (k == UINT64_MAX) {
        return UINT64_MAX;
    }
    overflow = false;
    credits = compute_level_credits(level_1_credits, ki_factor, reward_bps, k + 1, windows_per_year, &overflow);
    if (!overflow && (credits >= target_credits)) {
        return k + 1;
    }
    // Not expected to happen; report the target as unreachable so that the caller applies no limit
    return UINT64_MAX;
}
