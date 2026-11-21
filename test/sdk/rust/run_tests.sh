#!/bin/bash

# Runs the tests of the Shinobi Immortals Rust crate (sdk/rust).  See README.txt in this directory.
#
# Usage: test/sdk/rust/run_tests.sh
#
# Every step is run, even if one fails; the exit status is nonzero if any failed.  Set TEST_SEED to repeat a run with
# the same random inputs (each run prints its seed).

TEST_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SOURCE_ROOT=$(cd "$TEST_DIR/../../.." && pwd)
CRATE_DIR="$SOURCE_ROOT/sdk/rust"

# cargo is the requirement of every step; the tests check for the other programs that they need
if ! command -v cargo > /dev/null; then
    echo "FAIL: sdk rust tests: cargo is required"
    exit 1
fi

# Use one seed for all of the tests, so that a failing run can be repeated exactly
if [ -z "$TEST_SEED" ]; then
    export TEST_SEED=$(od -An -N8 -tx1 /dev/urandom | tr -d ' \n')
fi
echo "Running sdk rust tests with TEST_SEED=$TEST_SEED"

FAILED=

# The crate builds, without warnings, with each combination of its features: none (the core only), each feature
# alone, and both
for FEATURES in "--no-default-features" "--no-default-features --features solana" \
                "--no-default-features --features pinocchio" "--all-features"; do
    if RUSTFLAGS="-D warnings" cargo build --quiet --manifest-path "$CRATE_DIR/Cargo.toml" $FEATURES; then
        echo "PASS: sdk rust: build ($FEATURES)"
    else
        echo "FAIL: sdk rust: build ($FEATURES)"
        FAILED="$FAILED build($FEATURES)"
    fi
done

# The crate's documentation builds without warnings (every public item is documented, and every link resolves)
if RUSTDOCFLAGS="-D warnings" cargo doc --quiet --no-deps --all-features --manifest-path "$CRATE_DIR/Cargo.toml"; then
    echo "PASS: sdk rust: documentation"
else
    echo "FAIL: sdk rust: documentation"
    FAILED="$FAILED doc"
fi

# The tests (tests/*.rs), with the versions of their dependencies that Cargo.lock pins
if cargo test --quiet --locked --manifest-path "$TEST_DIR/Cargo.toml"; then
    echo "PASS: sdk rust: tests"
else
    echo "FAIL: sdk rust: tests"
    FAILED="$FAILED tests"
fi

# The example game programs build for the Solana target, which checks that the crate does (with the standard Solana
# crates, and with Pinocchio).  This needs cargo build-sbf, from the Solana tools, which is skipped if not installed.
if command -v cargo-build-sbf > /dev/null; then
    for EXAMPLE in example_game example_game_pinocchio; do
        # cargo build-sbf prints a lot even when it succeeds, so show its output only if it fails
        if OUTPUT=$(cargo build-sbf --manifest-path "$TEST_DIR/$EXAMPLE/Cargo.toml" -- --locked 2>&1); then
            echo "PASS: sdk rust: $EXAMPLE builds for the Solana target"
        else
            echo "$OUTPUT"
            echo "FAIL: sdk rust: $EXAMPLE builds for the Solana target"
            FAILED="$FAILED $EXAMPLE"
        fi
    done
else
    echo "SKIP: sdk rust: the example game programs (cargo build-sbf is not installed)"
fi

if [ -n "$FAILED" ]; then
    echo "FAIL: sdk rust tests:$FAILED"
    exit 1
fi

echo "PASS: sdk rust tests"
