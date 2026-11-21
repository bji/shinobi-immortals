// Tests that the calculations of the Rust crate (sdk/rust/src/calc.rs), which are ports of those of
// program/util/util_calculations.c, give the same results as the program.  It uses the reference vectors that
// test/program/calculation_vectors.sh generates for the program's own calculation tests (test/program/unit_tests.c),
// whose expected values that script computes independently, with dc.
//
// Only the operations that the crate mirrors are checked.  The level credit calculations (operations 6, 7, 9, and 10)
// and the checked arithmetic helpers (0, 1, and 8) have no counterparts in the crate.

mod common;

use shinobi_immortals::calc;
use std::collections::BTreeMap;
use std::process::Command;

/// Returns the operand as a u32, which the vectors of operations that take a u32 always give
fn as_u32(value: u64) -> u32 {
    u32::try_from(value).expect("a u32 operand")
}

/// Computes operation `operation` (by its number in test/program/unit_tests.c) of the operands with the crate,
/// returning its name and result, or None if the crate does not mirror the operation
fn compute(operation: u32, a: u64, b: u64, c: u64, d: u64) -> Option<(&'static str, u64)> {
    Some(match operation {
        2 => ("compute_price", calc::compute_price(a, b, c, d)),
        3 => ("compute_minimum_bid", calc::compute_minimum_bid(a, b)),
        4 => ("compute_effective_stake_earnings", calc::compute_effective_stake_earnings(a, b)),
        16 => ("compute_ki_harvest", calc::compute_ki_harvest(a, as_u32(b))),
        17 => ("compute_ki_harvest of compute_effective_stake_earnings",
               calc::compute_ki_harvest(calc::compute_effective_stake_earnings(a, b), as_u32(c))),
        18 => ("compute_ki_harvest_retention", calc::compute_ki_harvest_retention(as_u32(a), b)),
        19 => ("compute_retained_ki_harvest", calc::compute_retained_ki_harvest(a, as_u32(b), c)),
        20 => ("compute_measured_reward_rate", calc::compute_measured_reward_rate(a, b, c)),
        21 => ("compute_updated_reward_rate", calc::compute_updated_reward_rate(as_u32(a), b) as u64),
        22 => ("compute_reward_rate_bps", calc::compute_reward_rate_bps(as_u32(a)) as u64),
        _ => return None,
    })
}

/// The number of operations that the crate mirrors
const MIRRORED_OPERATIONS: usize = 10;


#[test]
fn calculations_match_the_program() {
    let output = Command::new("bash")
        .arg(common::source_root().join("test").join("program").join("calculation_vectors.sh"))
        .output().expect("running bash");
    assert!(output.status.success(), "calculation_vectors.sh failed (it needs dc)");
    let vectors = String::from_utf8(output.stdout).unwrap();

    let mut counts: BTreeMap<&str, usize> = BTreeMap::new();
    let mut failures = Vec::new();

    // Each vector is a pipe-delimited line: name, known defect, operation, initial overflow, expected failure,
    // expected overflow, the four operands, and the expected result
    for line in vectors.trim().lines() {
        let fields: Vec<&str> = line.split('|').collect();
        assert_eq!(fields.len(), 11, "a vector line: {}", line);
        let (name, defect, operation, expected_failure) = (fields[0], fields[1], fields[2], fields[4]);
        let operands: Vec<u64> = fields[6..10].iter().map(|f| f.parse().unwrap()).collect();
        let expected = fields[10];

        let Some((function, actual)) = compute(operation.parse().unwrap(), operands[0], operands[1], operands[2],
                                               operands[3]) else {
            continue;
        };
        *counts.entry(function).or_insert(0) += 1;

        // A vector of a known defect of the program expects the correct result, which the program does not give; the
        // crate mirrors the program, so these could not be checked.  There are none for these operations at present.
        if !defect.is_empty() || (expected_failure == "1") {
            failures.push(format!("{}: is a known defect ({}); update this test", name, defect));
            continue;
        }

        if actual.to_string() != expected {
            failures.push(format!("{} ({}): got {}, expected {} for {:?}", name, function, actual, expected, operands));
        }
    }

    // Every mirrored operation was exercised, so that a change to the vectors cannot silently skip one
    assert_eq!(counts.len(), MIRRORED_OPERATIONS, "operations exercised: {:?}", counts);
    println!("vectors checked: {:?}", counts);

    assert!(failures.is_empty(), "{} failures:\n{}", failures.len(), failures.join("\n"));
}


#[test]
fn lifespan_days() {
    // Whole days from the block's completion; none if the time is not later
    assert_eq!(calc::compute_lifespan_days(1000, 1000), 0);
    assert_eq!(calc::compute_lifespan_days(1000, 999), 0);
    assert_eq!(calc::compute_lifespan_days(1000, 1000 + 86399), 0);
    assert_eq!(calc::compute_lifespan_days(1000, 1000 + 86400), 1);
    assert_eq!(calc::compute_lifespan_days(-5, (86400 * 3) - 5), 3);
    assert_eq!(calc::compute_lifespan_days(i64::MIN, i64::MAX), u64::MAX / 86400);
}


#[test]
fn harvestable_ki() {
    let mut entry = common::make_entry(1, 2, 3, None);
    let mut block: shinobi_immortals::Block = bytemuck::Zeroable::zeroed();
    block.block_start_timestamp = 1_000_000;
    entry.level = 2;
    entry.metadata.level_metadata[2].ki_factor = 1000;
    entry.metadata.ki_harvest_reduction_per_day = 600_000;
    entry.owned.last_ki_harvest_stake_account_lamports = 10_000_000_000;

    // Not staked: nothing
    assert_eq!(calc::compute_harvestable_ki(&entry, &block, 11_000_000_000, 2_000_000), 0);

    entry.owned.stake_account = solana_address::Address::new_from_array([ 7; 32 ]);
    let now = 1_000_000 + (100 * 86400) + 5;
    let stake = 11_000_000_000;
    // The program's chain of calculations, step by step
    let effective = calc::compute_effective_stake_earnings(1_000_000_000, stake);
    let retention = calc::compute_ki_harvest_retention(600_000, 100);
    let expected = calc::compute_retained_ki_harvest(effective, 1000, retention);
    assert!(expected > 0);
    assert_eq!(calc::compute_harvestable_ki(&entry, &block, stake, now), expected);

    // A decrease in stake is not earnings
    assert_eq!(calc::compute_harvestable_ki(&entry, &block, 9_000_000_000, now), 0);

    // An invalid level harvests nothing, rather than panicking
    entry.level = 9;
    assert_eq!(calc::compute_harvestable_ki(&entry, &block, stake, now), 0);
}
