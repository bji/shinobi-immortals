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
// - With LEVEL_CREDIT_SCALE*N >= 10, the credit calculation's check that LEVEL_CREDIT_SCALE*N*(window Ki total) does
//   not overflow also guarantees that converting the applied whole Ki to deciKi (x10) for burning cannot overflow.
static_assert((LEVEL_CREDIT_WINDOW_SECONDS > 0) && ((SECONDS_PER_YEAR / LEVEL_CREDIT_WINDOW_SECONDS) >= 3),
              "LEVEL_CREDIT_WINDOW_SECONDS must be non-zero and give at least 3 level credit windows per year");
#define LEVEL_CREDIT_WINDOWS_PER_YEAR (SECONDS_PER_YEAR / LEVEL_CREDIT_WINDOW_SECONDS)

// K, the effective stake ceiling, in whole SOL.  A stake account's earnings earn Ki as if the stake account held its
// effective stake,
//   effective(x) = (K * x * (K + x)) / ((K * K) + (K * x) + (x * x))
// for a stake of x, which approaches K as x grows: a stake of 2K counts as about 0.857K, 10K as about 0.99K.  K is
// also the stake that the level credit curve is calibrated against: contributing the Ki that K effective SOL produces
// in a window, every window, levels fully in one year.  Both uses must always agree, which is why both are derived
// from this one value.
//
// The constraints on its value are those of the calculations below:
// - It must be a multiple of 10 SOL, and no more than 50 SOL, for compute_effective_stake_earnings: K is exact in
//   every unit that function uses, down to the coarsest (10 SOL), and its overflow bounds assume K <= 50 SOL.
// - 2000 must be a multiple of it, so that LEVEL_CREDIT_SCALE below is exact.
#define EFFECTIVE_STAKE_CEILING_SOL 50ul
static_assert((EFFECTIVE_STAKE_CEILING_SOL > 0) && ((EFFECTIVE_STAKE_CEILING_SOL % 10) == 0) &&
              (EFFECTIVE_STAKE_CEILING_SOL <= 50) && ((2000ul % EFFECTIVE_STAKE_CEILING_SOL) == 0),
              "EFFECTIVE_STAKE_CEILING_SOL must be a multiple of 10, at most 50, and a divisor of 2000");

// The factor that turns the level credit curve into exact integer form (see compute_level_credits).  It is
//   10000 / (5 * K):
// 40 for K = 50 SOL.
#define LEVEL_CREDIT_SCALE (2000ul / EFFECTIVE_STAKE_CEILING_SOL)

// A commission (commission_t, a uint16_t) is a fraction of staking rewards in which 0 means 0% and the largest value
// that can be stored, 65535, means 5%.  So no commission that can be stored is ever more than 5%: the cap is enforced
// by the type itself, and needs no check anywhere.  COMMISSION_FULL_SCALE is the value that would mean 100%, which is
// 20 times 65535 (100% / 5% = 20).  A commission of c is the fraction c / COMMISSION_FULL_SCALE of rewards.
#define COMMISSION_FULL_SCALE (65535ul * 20ul)

// The largest amount by which SetBlockCommission may raise a block's commission in one epoch: 1 percentage point.
// Since 65535 is 5%, 1% is 65535 / 5 = 13107 exactly.
#define COMMISSION_MAX_INCREASE_PER_EPOCH (65535ul / 5ul)

//   floor((value * numerator) / denominator),
// with
//   (numerator <= denominator)
// and
//   (denominator > 0).
// Reduce after each addition rather than forming a potentially overflowing product.
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
// total_seconds, which requires
//   start_price >= end_price.
// The curve is
//   y = (1 / (100x + 1)) - (1 / 101),
// where
//   x = seconds_elapsed / total_seconds;
// it runs from (100 / 101) at x = 0 down to 0 at x = 1.  The price is
//   end_price + ((start_price - end_price) * y),
// rounded down, computed in whole lamports.  So the price starts just below start_price (about 99% of the way from
// end_price to it) and falls to end_price; since the term added to end_price is never negative, the price is never
// below end_price.  Callers' prices are limited to 100,000,000 SOL (see admin_create_block), far below the roughly
// 182 million SOL at which the intermediate values below would overflow; if they ever did overflow, end_price is
// returned.
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

    // With
    //   x = (seconds_elapsed / total_seconds),
    // the amount added to end_price is
    //   delta * ((1 / (100x + 1)) - (1 / 101)) = (101 * delta - (100 * delta * x + delta)) / (101 * (100x + 1))
    // which is computed as
    //   (ac - ab) / bc,
    // with each time term rounded down:
    //   ac = 101 * delta
    uint64_t ac = checked_multiply(delta, 101ul, &overflow);

    //   ab = ((100 * delta * seconds_elapsed) / total_seconds) + delta.
    // The product
    //   100 * delta * seconds_elapsed
    // can overflow for large prices and long durations (for example, a 100,000 SOL start price after about 31
    // minutes), so it is never formed: multiply_fraction() computes the same floor without it.  It requires
    //   seconds_elapsed <= total_seconds
    // and
    //   total_seconds > 0,
    // both guaranteed by the early return above.  The result is at most
    //   100 * delta,
    // so ab is at most ac, and ac - ab cannot underflow.
    uint64_t ab = checked_add(multiply_fraction(checked_multiply(delta, 100ul, &overflow), seconds_elapsed,
                                                total_seconds),
                              delta, &overflow);

    // bc = ((100 * 101 * seconds_elapsed) / total_seconds) + 101, computed the same way
    uint64_t bc = checked_add(multiply_fraction(100ul * 101ul, seconds_elapsed, total_seconds), 101ul, &overflow);

    //   price = end_price + ((ac - ab) / bc).
    // The added term is at most delta, so the sum is at most start_price.
    uint64_t price = checked_add(end_price, (ac - ab) / bc, &overflow);

    // Defensive fallback only; no price accepted by admin_create_block can overflow
    if (overflow) {
        price = end_price;
    }

    return price;
}

// The smallest amount, in basis points of the current highest bid, by which a new auction bid must exceed it: 500 is
// 5%.  Every bid after the first must be at least the current highest bid plus this fraction of it (rounded up, and
// never less than 1 lamport).  Late bids are not made more expensive; instead a bid near the end of an auction
// extends it (see AUCTION_EXTENSION_SECONDS in constants.h), so that there is no advantage in bidding at the last
// moment.
#define MINIMUM_BID_INCREMENT_BPS 500ul

// Returns the smallest bid that may be placed on an auction whose current highest bid is current_max_bid (0 if there
// have been no bids), and whose first bid must be at least initial_minimum_bid.  Returns 0 if no bid is possible
// because the highest bid is already the largest possible.
static uint64_t compute_minimum_bid(uint64_t initial_minimum_bid, uint64_t current_max_bid)
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

    // The increment is current_max_bid * MINIMUM_BID_INCREMENT_BPS / 10000, rounded up.  The product can overflow,
    // so it is never formed: with current_max_bid = 10000q + r, the increment is q * BPS plus r * BPS / 10000 rounded
    // up.  q * BPS cannot overflow since q <= UINT64_MAX / 10000 and BPS <= 10000, and r * BPS is below 10^8.
    uint64_t q = current_max_bid / 10000ul;
    uint64_t r = current_max_bid % 10000ul;
    uint64_t increment = (q * MINIMUM_BID_INCREMENT_BPS) + (((r * MINIMUM_BID_INCREMENT_BPS) + 9999ul) / 10000ul);

    // A bid must always be strictly higher than the current highest bid, even if the increment rounds to nothing
    if (increment == 0) {
        increment = 1;
    }

    // The sum can overflow only for bids within a few percent of UINT64_MAX lamports; in that case the only bid that
    // can still exceed the current one is the largest possible
    bool overflow = false;
    uint64_t result = checked_add(current_max_bid, increment, &overflow);
    if (overflow) {
        result = UINT64_MAX;
    }

    return result;
}

// Scale actual stake earnings in lamports by effective stake / actual stake,
// returning effective stake earnings in lamports.
// For a stake of x and K = EFFECTIVE_STAKE_CEILING_SOL,
//   effective(x) = ((K * x) * (K + x)) / ((K * K) + (K * x) + (x * x)).
// Cancelling x gives the multiplier
//   (K * (K + x)) / ((K * K) + (K * x) + (x * x)),
// also defined at zero.
static uint64_t compute_effective_stake_earnings(uint64_t earnings_lamports, uint64_t stake_lamports)
{
    // The multiplier depends only on the ratio x / K, so x and K may be in any unit, as long as both use the same
    // one.  Use the finest unit in which the calculation below cannot overflow, so that rounding x down to a whole
    // number of units has no meaningful effect.  Start with units of 100 lamports, in which K (50 SOL) is
    // 500,000,000 units; then, for as long as x is more than 2,000,000,000 units, use units ten times larger for both.
    // - Stakes of up to 200 SOL use units of 100 lamports, so x is rounded down by less than 0.0000001 SOL.
    // - Larger stakes are rounded down by less than one unit out of at least 200,000,000, so by less than one part in
    //   200 million.
    // - Even UINT64_MAX lamports needs only 8 changes of unit, ending in units of 10 SOL, in which K is 5 units, so K
    //   is always exact (EFFECTIVE_STAKE_CEILING_SOL is a multiple of 10).
    uint64_t stake_units = stake_lamports / 100ul;
    uint64_t ceiling_units = EFFECTIVE_STAKE_CEILING_SOL * (1000000000ul / 100ul);
    while (stake_units > 2000000000ul) {
        stake_units /= 10;
        ceiling_units /= 10;
    }

    // With x <= 2,000,000,000 and K <= 500,000,000 units, the numerator is at most 1.25 * 10^18 and the denominator
    // at most 5.25 * 10^18.  The division below forms values up to (2 * denominator) + numerator, which is at most
    // 1.175 * 10^19, below 2^64 (about 1.845 * 10^19).
    uint64_t numerator = ceiling_units * (ceiling_units + stake_units);
    uint64_t denominator = numerator + (stake_units * stake_units);

    // Compute
    //   floor((earnings_lamports * numerator) / denominator)
    // without forming the product, which could overflow even though the final answer fits in uint64_t.  Process one
    // input bit at a time. The remainder stays below denominator; each step is below ((2 * denominator) + numerator)
    // and requires at most two subtractions.  Because numerator <= denominator, the final result fits in uint64_t.
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
// The exact curve caps effective earnings below K (50 SOL). Rounding its stake input down adds less than one unit of
// that calculation, which is at most 10 SOL (for enormous balances), so 60 SOL is a conservative bound for every valid
// uint64_t stake balance.
static uint64_t compute_ki_harvest(uint64_t effective_earnings_lamports, uint32_t ki_factor)
{
    // Avoid forming
    //   (effective_earnings_lamports * ki_factor):
    // that product can overflow even for 50 SOL of earnings and a large uint32_t factor.  The whole-SOL product is
    // below (60 * UINT32_MAX); the fractional product is below 1,000,000,000*UINT32_MAX. Both, and their sum after
    // division, fit.
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

    // Commission is the earned lamports times commission / COMMISSION_FULL_SCALE, rounded down (see
    // COMMISSION_FULL_SCALE: 65535 is 5%).  The product of the earned lamports and the commission can overflow (for
    // earnings above about 281,000 SOL at the maximum commission), so it is never formed: multiply_fraction()
    // computes the same floor without it.  It requires the numerator to be no more than the denominator, which holds
    // since commission is a uint16_t and so at most 65535, well below COMMISSION_FULL_SCALE.  The result is at most
    // 5% of the earned lamports.
    return multiply_fraction(current_stake - baseline, commission, COMMISSION_FULL_SCALE);
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

//   P(k) = (6 * L * k) / (N * ((5 * B) + k)),
// in tenths of a level credit.  L is the lifetime requirement, N windows per year, k the Ki contributed this window,
// and B the Ki produced in one window by a K effective SOL stake account, where K is EFFECTIVE_STAKE_CEILING_SOL (50
// SOL):
//   B = ki_factor * K * (R / N),
// where R = (reward_bps / 10000) is the expected annual reward rate.  Contributing B every window earns L/N per
// window, i.e. levels fully in one year; the most that can be earned in a window, however much Ki is contributed, is
//   ((6 * L) / N)
// (a minimum of N / 6 windows, about two months, to level fully).
//
// Substituting B gives
//   (5 * B) = (ki_factor * reward_bps) / (S * N),
// where
//   S = 10000 / (5 * K)
// is LEVEL_CREDIT_SCALE (40), and multiplying through by S * N gives an exact integer form with a single rounding:
//   P(k) = (6 * S * L * k) / (ki_factor * reward_bps + (S * N * k))
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
    uint64_t snk = checked_multiply(checked_multiply(windows_per_year, LEVEL_CREDIT_SCALE, overflow), window_ki,
                                    overflow);
    uint64_t denominator = checked_add(rate_term, snk, overflow);
    if (*overflow) {
        return 0;
    }
    // A uint32_t first-level cost gives L < 2^42; with S <= 200 (K >= 10 SOL), (6 * S * L) < 2^53 also fits uint64_t.
    uint64_t scaled_total = 6 * LEVEL_CREDIT_SCALE * compute_level_requirement(level_1_credits, 8);
    // window_ki <= (S * N * window_ki) < denominator (N >= 1 and rate_term > 0), as multiply_fraction requires.
    return multiply_fraction(scaled_total, window_ki, denominator);
}

// Returns the smallest window Ki total k for which
//   compute_level_credits(k) >= target_credits,
// i.e. the least Ki that the window must have received in total for its curve to reach target_credits.  Returns
// UINT64_MAX if no window Ki total can reach the target (the curve approaches, but never reaches, 6*L/N), or if the
// answer does not fit.
//
// With
//   A = (6 * S * L),
//   C = ki_factor * reward_bps, and
//   D = (S * N),
// where S is LEVEL_CREDIT_SCALE, the curve is
//   P(k) = floor((A * k) / (C + (D * k))).
// Because the target T is an integer, P(k) >= T exactly when
//   (A * k) >= (T * (C + (D * k))),
// i.e. when
//   (k * (A - (T * D))) >= (T * C).
// So the target is unreachable when
//   A <= (T * D),
// and otherwise the answer is
//   ceil((T * C) / (A - (T * D))).
// (T * C) can exceed 64 bits, so with
//   E = A - (T * D)
// it is computed as
//   T * (C / E) + (T * (C % E) / E),
// the second term by multiply_fraction since (C % E) < E.  The floor computed that way is then checked against
// compute_level_credits itself and stepped up by one if it falls short, so the result agrees exactly with the credits
// that will actually be awarded.
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
    // A = 6*S*L fits uint64_t (see compute_level_credits); C is a product of two uint32_t values
    uint64_t a = 6 * LEVEL_CREDIT_SCALE * compute_level_requirement(level_1_credits, 8);
    uint64_t c = (uint64_t) ki_factor * reward_bps;
    // T*D, where D = S*N.  If this overflows it certainly exceeds A, so the target is unreachable.
    uint64_t td = checked_multiply(checked_multiply(windows_per_year, LEVEL_CREDIT_SCALE, &overflow), target_credits,
                                   &overflow);
    if (overflow || (td >= a)) {
        return UINT64_MAX;
    }
    uint64_t e = a - td;

    // floor((T * C) / E) = (T * (C / E)) + floor((T * (C % E)) / E)
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
