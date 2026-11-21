'use strict';

// Tests that the calculations of sdk/js/shinobi_immortals_base.js, which mirror those of
// program/util/util_calculations.c, give the same results as the program.  It uses the same reference vectors that
// test/program/calculation_vectors.sh generates for the program's calculation tests (test/program/unit_tests.c),
// whose expected values that script computes independently, with dc.
//
// Only the operations that the SDK mirrors are checked.  The level credit calculations (operations 6, 7, 9, and 10)
// and the checked arithmetic helpers (0, 1, and 8) have no Javascript counterparts.

const path = require("path");
const { execFileSync } = require("child_process");
const { SOURCE_ROOT, Base, Results, require_tools } = require("./common.js");

const results = new Results("calculations");

require_tools(results.name, [ "bash", "dc" ]);


// Each operation that the SDK mirrors, by its number in test/program/unit_tests.c: its name, and a function of the
// vector's four operands (BigInts) that computes the result with the SDK
const OPERATIONS = {
    2 : [ "compute_price", (a, b, c, d) => Base.compute_price(a, b, c, d) ],
    3 : [ "compute_minimum_bid", (a, b) => Base.compute_minimum_bid(a, b) ],
    4 : [ "compute_effective_stake_earnings", (a, b) => Base.compute_effective_stake_earnings(a, b) ],
    16 : [ "compute_ki_harvest", (a, b) => Base.compute_ki_harvest(a, b) ],
    17 : [ "compute_ki_harvest of compute_effective_stake_earnings",
           (a, b, c) => Base.compute_ki_harvest(Base.compute_effective_stake_earnings(a, b), c) ],
    18 : [ "compute_ki_harvest_retention", (a, b) => Base.compute_ki_harvest_retention(a, b) ],
    19 : [ "compute_retained_ki_harvest", (a, b, c) => Base.compute_retained_ki_harvest(a, b, c) ],
    20 : [ "compute_measured_reward_rate", (a, b, c) => Base.compute_measured_reward_rate(a, b, c) ],
    21 : [ "compute_updated_reward_rate", (a, b) => Base.compute_updated_reward_rate(a, b) ],
    22 : [ "compute_reward_rate_bps", (a) => Base.compute_reward_rate_bps(a) ]
};


// The vectors are pipe-delimited lines: name, known defect, operation, initial overflow, expected failure, expected
// overflow, the four operands, and the expected result
let lines = execFileSync("bash", [ path.join(SOURCE_ROOT, "test", "program", "calculation_vectors.sh") ],
                         { maxBuffer : 1 << 26 }).toString().trim().split("\n");

let counts = { };
for (let line of lines) {
    let [ name, defect, operation, , expected_failure, , a, b, c, d, expected ] = line.split("|");

    if (!(operation in OPERATIONS)) {
        continue;
    }

    let [ function_name, compute ] = OPERATIONS[operation];
    counts[function_name] = (counts[function_name] || 0) + 1;

    // A vector of a known defect of the program expects the correct result, which the program does not give; the
    // SDK mirrors the program, so these are not checked.  There are none for these operations at present.
    if (defect || (expected_failure == "1")) {
        results.check(name + " is not a known defect", false, "known defect " + defect + "; update this test");
        continue;
    }

    let actual;
    try {
        actual = compute(BigInt(a), BigInt(b), BigInt(c), BigInt(d));
    }
    catch (error) {
        actual = "threw " + error.message;
    }
    results.check(name + " (" + function_name + ")", String(actual) == expected,
                  "got " + actual + ", expected " + expected + " for " + [ a, b, c, d ].join(", "));
}

// Make sure that every operation was actually exercised, so that a change to the vectors can't silently skip one
for (let [ function_name ] of Object.values(OPERATIONS)) {
    results.check("vectors exercise " + function_name, (counts[function_name] || 0) > 0);
}
if (process.env.VERBOSE) {
    console.log("Vectors checked: " + JSON.stringify(counts));
}


results.finish();
