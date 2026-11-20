#!/bin/bash

# Runs the tests of the Shinobi Immortals Javascript SDK (sdk/js).  See README.txt in this directory.
#
# Usage: test/sdk/js/run_tests.sh
#
# Every test is run, even if one fails; the exit status is nonzero if any failed.  Set VERBOSE=1 to see every check,
# and TEST_SEED to repeat a run with the same random inputs (each run prints its seed).

TEST_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# Node is the only requirement of every test; each test checks for any other program that it needs
if ! command -v node > /dev/null; then
    echo "FAIL: sdk js tests: node is required"
    exit 1
fi

# Use one seed for all of the tests, so that a failing run can be repeated exactly
if [ -z "$TEST_SEED" ]; then
    export TEST_SEED=$(od -An -N8 -tx1 /dev/urandom | tr -d ' \n')
fi
echo "Running sdk js tests with TEST_SEED=$TEST_SEED"

# The tests that need packages from npm (test_kit, test_rpc, and test_wallet) skip themselves if those are not
# installed
FAILED=
for TEST in test_primitives test_program_consistency test_calculations test_instructions test_kit test_rpc \
            test_wallet; do
    node "$TEST_DIR/$TEST.js" || FAILED="$FAILED $TEST"
done

if [ -n "$FAILED" ]; then
    echo "FAIL: sdk js tests:$FAILED"
    exit 1
fi

echo "PASS: sdk js tests"
