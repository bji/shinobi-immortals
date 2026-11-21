//! The program's calculations: sale prices, minimum bids, effective stake, and Ki harvests.
//!
//! Each function is a direct port of the function of the same name in `program/util/util_calculations.c`, with the
//! same integer types and the same order of operations, so that it gives exactly the program's result for every input
//! (including the program's fallbacks for values that would overflow).  The tests check them against the reference
//! vectors that `test/program/calculation_vectors.sh` generates for the program's own calculation tests.

use crate::state::{Block, Entry};

/// One, in the units of Ki harvest reductions and retentions, which are fractions of `u32::MAX`
/// (`KI_HARVEST_RETENTION_ONE`)
pub const KI_HARVEST_RETENTION_ONE: u64 = u32::MAX as u64;

/// The steepness of the sale price curve (`PRICE_CURVE_STEEPNESS`; see [`compute_price`])
pub const PRICE_CURVE_STEEPNESS: u64 = 1069;

/// The least increase of each bid over the one before, in basis points (`MINIMUM_BID_INCREMENT_BPS`): 5%
pub const MINIMUM_BID_INCREMENT_BPS: u64 = 500;

/// The effective stake ceiling, in whole SOL (`EFFECTIVE_STAKE_CEILING_SOL`; see [`compute_effective_stake_earnings`])
pub const EFFECTIVE_STAKE_CEILING_SOL: u64 = 50;

/// Lamports per SOL
pub const LAMPORTS_PER_SOL: u64 = 1_000_000_000;

/// Seconds per day, the unit of an entry's life span
pub const SECONDS_PER_DAY: u64 = 24 * 60 * 60;


/// Returns `floor((value * numerator) / denominator)` without forming the product, which may overflow; requires
/// `numerator <= denominator` and `denominator > 0`.  `multiply_fraction` of the program.
fn multiply_fraction(value: u64, numerator: u64, denominator: u64) -> u64 {
    let mut remainder: u64 = 0;
    let mut result: u64 = 0;
    for bit in (1..=64u32).rev() {
        let mut digit = 0;
        // Double the remainder modulo denominator without overflowing
        if remainder >= denominator - remainder {
            remainder -= denominator - remainder;
            digit = 1;
        }
        else {
            remainder += remainder;
        }
        if ((value >> (bit - 1)) & 1) != 0 {
            if remainder >= denominator - numerator {
                remainder -= denominator - numerator;
                digit += 1;
            }
            else {
                remainder += numerator;
            }
        }
        result = (result << 1).wrapping_add(digit);
    }
    result
}


/// Returns the price at `seconds_elapsed` into a sale whose price falls from `start_price` to `end_price` over
/// `total_seconds` (which requires `start_price >= end_price`).  With T = `total_seconds`, t = `seconds_elapsed`, and
/// k = [`PRICE_CURVE_STEEPNESS`], the price is
///
/// ```text
/// end_price + floor(floor((start_price - end_price) * T / (kt + T)) * k(T - t) / ((k + 1)T))
/// ```
///
/// which falls quickly at first and then more and more slowly, reaching `end_price` at the end of the sale.  This is
/// how the price of a mystery, and of a revealed entry without an auction, is computed.
pub fn compute_price(total_seconds: u64, start_price: u64, end_price: u64, seconds_elapsed: u64) -> u64 {
    if seconds_elapsed >= total_seconds {
        return end_price;
    }

    let delta = start_price.wrapping_sub(end_price);

    // kt + T, k(T - t), and (k + 1)T; if any overflows, the program uses the end price
    let first_denominator = PRICE_CURVE_STEEPNESS.checked_mul(seconds_elapsed)
        .and_then(|kt| kt.checked_add(total_seconds));
    let second_numerator = PRICE_CURVE_STEEPNESS.checked_mul(total_seconds - seconds_elapsed);
    let second_denominator = (PRICE_CURVE_STEEPNESS + 1).checked_mul(total_seconds);
    let (Some(first_denominator), Some(second_numerator), Some(second_denominator)) =
        (first_denominator, second_numerator, second_denominator) else {
        return end_price;
    };

    let added = multiply_fraction(delta, total_seconds, first_denominator);
    let added = multiply_fraction(added, second_numerator, second_denominator);

    end_price.wrapping_add(added)
}


/// Returns the least bid that can be made on an entry whose highest bid so far is `current_max_bid` (0 if none) and
/// whose minimum price is `initial_minimum_bid`: the minimum price for the first bid, and then at least
/// [`MINIMUM_BID_INCREMENT_BPS`] more than the highest bid (rounded up, and at least 1 lamport more), saturating at
/// `u64::MAX`.  Returns 0 if no higher bid is possible.
// Written as the program's C is, rather than with div_ceil and saturating_add, so that the two can be compared line
// by line
#[allow(clippy::manual_div_ceil, clippy::manual_saturating_arithmetic)]
pub fn compute_minimum_bid(initial_minimum_bid: u64, current_max_bid: u64) -> u64 {
    if current_max_bid == u64::MAX {
        return 0;
    }

    if current_max_bid < initial_minimum_bid {
        return initial_minimum_bid;
    }

    // current_max_bid * BPS / 10000, rounded up, without forming the product
    let q = current_max_bid / 10000;
    let r = current_max_bid % 10000;
    let mut increment = (q * MINIMUM_BID_INCREMENT_BPS) + (((r * MINIMUM_BID_INCREMENT_BPS) + 9999) / 10000);

    if increment == 0 {
        increment = 1;
    }

    current_max_bid.checked_add(increment).unwrap_or(u64::MAX)
}


/// Returns the earnings that count towards Ki of a stake account holding `stake_lamports` that earned
/// `earnings_lamports`: the earnings scaled by K(K + x) / (K(K + x) + x^2), for x the stake and K
/// [`EFFECTIVE_STAKE_CEILING_SOL`], so that a stake account earns Ki as if it held at most about K SOL.
pub fn compute_effective_stake_earnings(earnings_lamports: u64, stake_lamports: u64) -> u64 {
    // The finest units (starting at 100 lamports) in which the calculation cannot overflow
    let mut stake_units = stake_lamports / 100;
    let mut ceiling_units = EFFECTIVE_STAKE_CEILING_SOL * (LAMPORTS_PER_SOL / 100);
    while stake_units > 2_000_000_000 {
        stake_units /= 10;
        ceiling_units /= 10;
    }

    let numerator = ceiling_units * (ceiling_units + stake_units);
    let denominator = numerator + (stake_units * stake_units);

    // floor((earnings_lamports * numerator) / denominator), one bit at a time, without forming the product
    let mut remainder: u64 = 0;
    let mut result: u64 = 0;
    for bit in (1..=64u32).rev() {
        remainder = (remainder << 1).wrapping_add(if ((earnings_lamports >> (bit - 1)) & 1) != 0 { numerator }
                                                  else { 0 });
        let mut digit = 0;
        if remainder >= denominator {
            remainder -= denominator;
            digit = 1;
            if remainder >= denominator {
                remainder -= denominator;
                digit = 2;
            }
        }
        result = (result << 1).wrapping_add(digit);
    }

    result
}


/// Returns the whole Ki earned by `effective_earnings_lamports` of effective stake earnings at `ki_factor` Ki per SOL,
/// rounded down
pub fn compute_ki_harvest(effective_earnings_lamports: u64, ki_factor: u32) -> u64 {
    let whole_sol = effective_earnings_lamports / LAMPORTS_PER_SOL;
    let remaining_lamports = effective_earnings_lamports % LAMPORTS_PER_SOL;
    whole_sol.wrapping_mul(ki_factor as u64)
        .wrapping_add(remaining_lamports.wrapping_mul(ki_factor as u64) / LAMPORTS_PER_SOL)
}


/// Returns the fraction, of [`KI_HARVEST_RETENTION_ONE`], of a Ki harvest that an entry retains when its life span is
/// `lifespan_days` whole days and its harvests are reduced by `reduction_per_day` (a fraction of
/// [`KI_HARVEST_RETENTION_ONE`]) per day: `(1 - reduction_per_day)^lifespan_days`, rounded down at each step as the
/// program does.
pub fn compute_ki_harvest_retention(reduction_per_day: u32, lifespan_days: u64) -> u64 {
    let mut base = KI_HARVEST_RETENTION_ONE - (reduction_per_day as u64);
    let mut result = KI_HARVEST_RETENTION_ONE;
    let mut days = lifespan_days;
    while days != 0 {
        if (days & 1) != 0 {
            result = (result * base) / KI_HARVEST_RETENTION_ONE;
        }
        if result == 0 {
            break;
        }
        base = (base * base) / KI_HARVEST_RETENTION_ONE;
        days >>= 1;
    }
    result
}


/// Returns the whole Ki earned by `effective_earnings_lamports` of effective stake earnings at `ki_factor` Ki per SOL,
/// after reduction to `retention` (from [`compute_ki_harvest_retention`]).  The reduction is applied to the earnings,
/// so that only the final result is rounded to whole Ki.
pub fn compute_retained_ki_harvest(effective_earnings_lamports: u64, ki_factor: u32, retention: u64) -> u64 {
    let retention = retention.min(KI_HARVEST_RETENTION_ONE);
    let retained_earnings_lamports = multiply_fraction(effective_earnings_lamports, retention,
                                                       KI_HARVEST_RETENTION_ONE);
    compute_ki_harvest(retained_earnings_lamports, ki_factor)
}


/// Returns an entry's life span in whole days at time `unix_timestamp`, for an entry whose block was completed at
/// `block_start_timestamp` (0 if the time is not later), as the program computes it when harvesting Ki
pub fn compute_lifespan_days(block_start_timestamp: i64, unix_timestamp: i64) -> u64 {
    if unix_timestamp > block_start_timestamp {
        (unix_timestamp.wrapping_sub(block_start_timestamp) as u64) / SECONDS_PER_DAY
    }
    else {
        0
    }
}


/// Returns the whole Ki that the Harvest instruction would harvest now from a staked `entry` (of block `block`) whose
/// stake account's delegated stake is `delegated_stake_lamports`, at time `unix_timestamp`: the effective earnings
/// since the last harvest, at the Ki factor of the entry's current level, reduced for the entry's life span.  This is
/// the calculation of `harvest_ki` in `program/util/util_ki.c`.
///
/// `delegated_stake_lamports` is the stake account's `delegation.stake`, not its lamports.  This assumes what the
/// program checks of the stake account before harvesting: that it is delegated to Shinobi Systems
/// ([`crate::id::SHINOBI_SYSTEMS_VOTE`]), and that its activation epoch is before the current epoch (otherwise the
/// program harvests nothing).  Returns 0 for an entry that is not staked or whose level is not valid, and when the
/// delegated stake is less than at the last harvest (which the program treats as no earnings).
pub fn compute_harvestable_ki(entry: &Entry, block: &Block, delegated_stake_lamports: u64, unix_timestamp: i64)
                              -> u64 {
    if !entry.is_staked() {
        return 0;
    }
    let Some(level) = entry.metadata.level_metadata.get(entry.level as usize) else {
        return 0;
    };
    // A decrease in delegated stake is not earnings
    let Some(actual_earnings_lamports) =
        delegated_stake_lamports.checked_sub(entry.owned.last_ki_harvest_stake_account_lamports) else {
        return 0;
    };
    let effective_earnings_lamports = compute_effective_stake_earnings(actual_earnings_lamports,
                                                                       delegated_stake_lamports);
    let lifespan_days = compute_lifespan_days(block.block_start_timestamp, unix_timestamp);
    let retention = compute_ki_harvest_retention(entry.metadata.ki_harvest_reduction_per_day, lifespan_days);
    compute_retained_ki_harvest(effective_earnings_lamports, level.ki_factor, retention)
}
