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

// A day, in seconds
#define SECONDS_PER_DAY (24ul * 60ul * 60ul)

// A 365 day year, in seconds
#define SECONDS_PER_YEAR (365ul * SECONDS_PER_DAY)

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

// The steepness k of the sale price curve (see compute_price), whose shape is
//   y = 1/(kx + 1) - 1/(k + 1),
// x being the fraction of the sale elapsed.  1069 makes the price fall to 1/10 of the way from the minimum to the
// start price after 1/120 of the sale: 2 hours of a 10 day sale, or 1 hour of a 5 day sale.  (k is the larger root of
// k^2 - 1069k + 120 = 0, which is 1068.9.)  After that the price keeps falling, more and more slowly, through the
// rest of the sale, down to the minimum at its end.  An earlier version of this program used k = 100.
#define PRICE_CURVE_STEEPNESS 1069ul

// Returns the price at seconds_elapsed into a price curve that falls from start_price to end_price over
// total_seconds, which requires
//   start_price >= end_price.
// With
//   T = total_seconds,
//   t = seconds_elapsed,
//   x = t / T, and
//   k = PRICE_CURVE_STEEPNESS,
// the curve is
//   y = 1/(kx + 1) - 1/(k + 1) = k(T - t) / ((kt + T)(k + 1)),
// which runs from k / (k + 1), just below 1, at t = 0, down to 0 at t = T, always falling.  The price is
//   end_price + (start_price - end_price) * y,
// computed in whole lamports as
//   end_price + floor(floor((start_price - end_price) * T / (kt + T)) * k(T - t) / ((k + 1)T)).
// Since each of the two factors is at most 1, the price is never below end_price and never above start_price.  Each
// step is computed with multiply_fraction(), and kt + T, k(T - t) and (k + 1)T are all far below 2^64 for any
// duration that a block can have (durations are uint32_t), so no intermediate value can overflow.
static uint64_t compute_price(uint64_t total_seconds, uint64_t start_price, uint64_t end_price,
                              uint64_t seconds_elapsed)
{
    // Once the elapsed seconds hits the total seconds, the result is always end_price
    if (seconds_elapsed >= total_seconds) {
        return end_price;
    }

    // Keep track of overflow, and if it occurs, use the end price
    bool overflow = false;

    // The amount by which the price falls over the whole curve
    uint64_t delta = start_price - end_price;

    // The numerators and denominators of the two factors of the curve.  T - t cannot underflow because of the early
    // return above.
    //   kt + T
    uint64_t first_denominator = checked_add(checked_multiply(PRICE_CURVE_STEEPNESS, seconds_elapsed, &overflow),
                                             total_seconds, &overflow);
    //   k(T - t)
    uint64_t second_numerator = checked_multiply(PRICE_CURVE_STEEPNESS, total_seconds - seconds_elapsed, &overflow);
    //   (k + 1)T
    uint64_t second_denominator = checked_multiply(PRICE_CURVE_STEEPNESS + 1, total_seconds, &overflow);

    // Defensive fallback only; no duration that a block can have can overflow
    if (overflow) {
        return end_price;
    }

    // First factor: delta * T / (kt + T).  multiply_fraction() requires T <= kt + T, which is always true, and
    // kt + T > 0, which is true since T > 0 (T > t >= 0, from the early return above).
    uint64_t added = multiply_fraction(delta, total_seconds, first_denominator);

    // Second factor: * k(T - t) / ((k + 1)T).  multiply_fraction() requires k(T - t) <= (k + 1)T, which is always
    // true since T - t <= T.
    added = multiply_fraction(added, second_numerator, second_denominator);

    // The added term is at most delta, so the sum is at most start_price and cannot overflow
    return end_price + added;
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

// The unit in which Ki harvest reductions and retentions are expressed: each is a fraction of UINT32_MAX, so this
// value is one (all of a harvest)
#define KI_HARVEST_RETENTION_ONE ((uint64_t) UINT32_MAX)

// Returns the fraction, as a fraction of UINT32_MAX, of a Ki harvest that is retained by an entry whose life span is
// lifespan_days whole days and whose Ki harvests are reduced by reduction_per_day (a fraction of UINT32_MAX) per day.
// With
//   q = (UINT32_MAX - reduction_per_day),
// the fraction retained in each day, it is approximately
//   UINT32_MAX * (q / UINT32_MAX)^lifespan_days.
// A reduction of UINT32_MAX retains nothing after the first day.  The power is computed by repeated squaring, with
// each multiplication of two such fractions rounded down to a whole fraction of UINT32_MAX:
//   result = UINT32_MAX, base = q
//   for each bit of lifespan_days, starting from the least significant:
//     if the bit is set, result = floor((result * base) / UINT32_MAX)
//     base = floor((base * base) / UINT32_MAX)
// Every value is at most one, so each multiplication passes on no more than the sum of its inputs' errors, and each
// rounding adds less than one unit (1 / UINT32_MAX).  So the k'th base is low by less than 2^k units, and the result,
// which is the product of the bases for the set bits, is never above the exact value and is low by less than
// lifespan_days + 1 units: about 0.0009% after a century.  All values are at most UINT32_MAX, so every product is less
// than 2^64 and cannot overflow.
static uint64_t compute_ki_harvest_retention(uint32_t reduction_per_day, uint64_t lifespan_days)
{
    // The fraction retained in each day; nothing at all if the reduction is everything
    uint64_t base = KI_HARVEST_RETENTION_ONE - reduction_per_day;

    // Raise base to the power lifespan_days, one bit of the exponent at a time
    uint64_t result = KI_HARVEST_RETENTION_ONE;
    while (lifespan_days) {
        if (lifespan_days & 1) {
            result = (result * base) / KI_HARVEST_RETENTION_ONE;
        }
        // Once nothing is retained, nothing ever will be; stop early (the result would be the same)
        if (!result) {
            break;
        }
        base = (base * base) / KI_HARVEST_RETENTION_ONE;
        lifespan_days >>= 1;
    }

    return result;
}

// Returns the whole Ki earned by effective_earnings_lamports of effective stake earnings, at ki_factor Ki per SOL,
// after reduction to retention (a fraction of UINT32_MAX, as returned by compute_ki_harvest_retention).  The reduction
// is applied to the earnings before they are converted to Ki, so that only the final result is rounded to whole Ki.
static uint64_t compute_retained_ki_harvest(uint64_t effective_earnings_lamports, uint32_t ki_factor,
                                            uint64_t retention)
{
    // compute_ki_harvest_retention never returns more than KI_HARVEST_RETENTION_ONE; but be certain, since
    // multiply_fraction requires that the numerator be no more than the denominator
    if (retention > KI_HARVEST_RETENTION_ONE) {
        retention = KI_HARVEST_RETENTION_ONE;
    }

    // The retained earnings are no more than the effective earnings, so compute_ki_harvest's bounds still hold
    uint64_t retained_earnings_lamports =
        multiply_fraction(effective_earnings_lamports, retention, KI_HARVEST_RETENTION_ONE);

    return compute_ki_harvest(retained_earnings_lamports, ki_factor);
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

// The reward rate that MeasureRewardRate measures, and LevelUp uses, is an annual rate held as a binary fraction: a
// value of r is the rate r / 2^32 per year.  It is kept in a uint32_t, and so must be below 1 (100% per year); it is
// always kept between REWARD_RATE_MIN and REWARD_RATE_MAX.  This much precision (about 0.000002 basis points) is used
// so that the smoothing of measurements (see compute_updated_reward_rate), which rounds down, cannot noticeably bias the
// rate downwards.
#define REWARD_RATE_ONE (1ul << 32)

// The bounds of the reward rate, in basis points, and as reward rates.  The rate that Initialize is given must be
// within them too.  A measurement outside of these is taken to be
// these: 1% is safely below the yield that staking is ever expected to fall to (inflation is scheduled to fall to 1.5%,
// and yield is that divided by the fraction of SOL staked), and 20% is far above any yield since inflation began.  The
// lower bound is the one that matters most, since a lower rate makes levelling easier.
#define REWARD_RATE_MIN_BPS 100ul
#define REWARD_RATE_MAX_BPS 2000ul
#define REWARD_RATE_MIN ((REWARD_RATE_MIN_BPS * REWARD_RATE_ONE) / 10000ul)
#define REWARD_RATE_MAX ((REWARD_RATE_MAX_BPS * REWARD_RATE_ONE) / 10000ul)
static_assert((REWARD_RATE_MIN > 0) && (REWARD_RATE_MIN < REWARD_RATE_MAX) && (REWARD_RATE_MAX <= UINT32_MAX),
              "The reward rate bounds must be ordered, non-zero, and fit in a uint32_t");

// The most, in percent, by which a single measurement may move the reward rate before smoothing.  Real reward rates
// move by about 0.2% per level credit window, so this lets the rate follow them easily, while limiting the effect of
// a single unusual measurement (for example, one covering an epoch in which Shinobi Systems was briefly delinquent).
#define REWARD_RATE_MAX_CHANGE_PERCENT 5ul

// The smoothing of measurements: each new rate is (REWARD_RATE_SMOOTHING - 1) parts the previous rate to 1 part the
// (limited) measurement.  With the limit above, the rate moves by at most 5%/4 = 1.25% per measurement.
#define REWARD_RATE_SMOOTHING 4ul

// Returns the annual reward rate (see REWARD_RATE_ONE) of a stake account whose delegated stake grew from
// [snapshot_stake] to [current_stake] in [seconds]:
//   ((current_stake - snapshot_stake) / snapshot_stake) * (SECONDS_PER_YEAR / seconds) * 2^32,
// which is the simple (not compounded) annual rate, matching the way that the level credit curve uses the rate (the
// rate per window is the annual rate divided by the windows per year).  Rounds down.  Returns UINT64_MAX, which is
// above any rate that is kept, if the stake more than doubled, and 0 for invalid inputs (no snapshot stake, no time,
// or a decrease in stake), none of which the caller passes.
static uint64_t compute_measured_reward_rate(uint64_t snapshot_stake, uint64_t current_stake, uint64_t seconds)
{
    if (!snapshot_stake || !seconds || (current_stake < snapshot_stake)) {
        return 0;
    }

    uint64_t growth = current_stake - snapshot_stake;

    // multiply_fraction requires growth <= snapshot_stake; a rate that high is far above REWARD_RATE_MAX anyway
    if (growth > snapshot_stake) {
        return UINT64_MAX;
    }

    // 2^32 * SECONDS_PER_YEAR is about 1.35e17, so it fits a uint64_t; the fraction of it is computed without
    // overflow, and is no more than it
    return multiply_fraction(REWARD_RATE_ONE * SECONDS_PER_YEAR, growth, snapshot_stake) / seconds;
}

// Returns the new reward rate, given the previous rate [previous_rate] (the rate that Initialize set, or the result of
// the previous measurement, so always a kept rate) and a new measurement [measured_rate] (see
// compute_measured_reward_rate):
// - The measurement is first limited to within REWARD_RATE_MAX_CHANGE_PERCENT of the previous rate.
// - It is then smoothed: the new rate is ((REWARD_RATE_SMOOTHING - 1) * previous + limited) / REWARD_RATE_SMOOTHING.
// - The result is then kept between REWARD_RATE_MIN and REWARD_RATE_MAX.
// Every measurement, including the first, is treated this way, so that no single measurement can move the rate by more
// than 1.25%.  All values are far below 2^64 at every step: rates are below 2^32.
static uint32_t compute_updated_reward_rate(uint32_t previous_rate, uint64_t measured_rate)
{
    uint64_t previous = previous_rate;
    uint64_t lowest = (previous * (100ul - REWARD_RATE_MAX_CHANGE_PERCENT)) / 100ul;
    uint64_t highest = (previous * (100ul + REWARD_RATE_MAX_CHANGE_PERCENT)) / 100ul;
    uint64_t limited = (measured_rate < lowest) ? lowest : ((measured_rate > highest) ? highest : measured_rate);
    uint64_t rate = ((previous * (REWARD_RATE_SMOOTHING - 1)) + limited) / REWARD_RATE_SMOOTHING;

    if (rate < REWARD_RATE_MIN) {
        rate = REWARD_RATE_MIN;
    }
    else if (rate > REWARD_RATE_MAX) {
        rate = REWARD_RATE_MAX;
    }

    return (uint32_t) rate;
}

// Returns a reward rate (see REWARD_RATE_ONE) in basis points, rounded to the nearest basis point (halves round up).
// This is the form that the level credit curve takes (see compute_level_credits).  Kept rates are at least 100 basis
// points, so the result is never zero for them.
static uint32_t compute_reward_rate_bps(uint32_t rate)
{
    return (uint32_t) ((((uint64_t) rate * 10000ul) + (REWARD_RATE_ONE / 2)) / REWARD_RATE_ONE);
}
