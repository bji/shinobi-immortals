Tests of the Shinobi Immortals Rust crate (sdk/rust)
-----------------------------------------------------

Run them all with:

    test/sdk/rust/run_tests.sh

or with "make test-sdk-rust".  They take about half a minute, most of it generating the calculation vectors.

They need cargo, bash, dc, a C compiler (cc), and solxact (https://github.com/bji/solxact), all but cargo of which
the program tests (test/program) need too.  The example game programs also need cargo build-sbf, from the Solana
tools; they are skipped (and say so) if it is not installed.  The first run downloads the crates that the Cargo.lock
files pin, if cargo does not already have them.  "make clean-test-sdk-rust" removes cargo's build output.

run_tests.sh checks that:

  - The crate builds without warnings with each combination of its features (none, "solana", "pinocchio", and both),
    and its documentation builds without warnings.

  - The tests of tests/ pass (the test crate here depends on sdk/rust, so that the published crate carries none of
    this repository's test machinery):

      layouts.rs           Every account structure against the program's C structures, as the C compiler lays them
                           out: c/layouts.c, compiled with the program's own headers, prints each structure's size and
                           alignment, and every field's offset, size, and value in an instance filled with random
                           bytes (with signed fields negative), which the Rust structures must match exactly.

      instructions.rs      Every instruction builder, with its options, against the transaction that the
                           corresponding script in scripts/ makes, decoded by "solxact decode": the program, every
                           account and its permissions, and the data; and that every instruction is covered.

      calculations.rs      The calculations against the reference vectors that test/program/calculation_vectors.sh
                           generates for the program's own calculation tests.

      addresses.rs         The fixed addresses and bump seeds against those compiled into the program
                           (build_program.sh), and every address derivation against "solxact pda".

      load.rs              Checked loading and verification of Immortals, with raw account parts, AccountInfo, and
                           Pinocchio's AccountView, which must all agree: genuine accounts are accepted, and each kind
                           of forged or unsuitable account (owned by another program, of another data type, too
                           short, reauthorized, or with a token account that is forged, of another mint or wallet,
                           empty, or not a token account) is rejected.  Also the methods of the account types,
                           including the entry states.

  - The example game programs, example_game (built with the standard Solana crates) and example_game_pinocchio (built
    with Pinocchio), build for the Solana target.  Each verifies a player's Immortal as a game should, and they are
    examples for game authors as much as tests.

The tests use random inputs from a seeded generator; a failing test prints its seed (TEST_SEED=...), and setting
TEST_SEED to it repeats the test exactly.  A single test file can be run with, for example,
"cargo test --manifest-path test/sdk/rust/Cargo.toml --test load".
