#!/bin/bash
# Standalone framework regression tests; no validator or Solana tools required.
set -e
source "$(dirname "${BASH_SOURCE[0]}")/common"

CONFIRMATION_TMP=$(mktemp -d)
trap 'rm -rf "$CONFIRMATION_TMP"' EXIT

# The counter lives in a file because polling runs inside command substitution.
function curl ()
{
    local n=$(cat "$CONFIRMATION_TMP/count")
    echo $((n + 1)) > "$CONFIRMATION_TMP/count"
    case "$n" in
        0) return 7 ;; # Transport failure
        1) echo '' ;;
        2) echo 'not json' ;;
        3) echo '{"error":{"code":-32000}}' ;;
        4) echo '{"result":null}' ;;
        5) echo '{"result":{"meta":null}}' ;;
        6) echo '{"result":{"meta":{}}}' ;;
        *) echo "$CONFIRMATION_RESPONSE" ;;
    esac
}

function sleep () { :; }

function check_assertion ()
{
    local expected_status=$1
    local expected_attempts=$2
    shift 2
    echo 0 > "$CONFIRMATION_TMP/count"
    local status=0
    # Each assertion receives a successful submission status, as in the suite.
    ( true; "$@" ) > "$CONFIRMATION_TMP/output" 2>&1 || status=$?
    if [ "$status" -ne "$expected_status" ] ||
       [ "$(cat "$CONFIRMATION_TMP/count")" -ne "$expected_attempts" ]; then
        cat "$CONFIRMATION_TMP/output"
        echo "FAIL: unexpected status or polling count for $*"
        exit 1
    fi
}

CONFIRMATION_RESPONSE='{"result":{"meta":{"err":null}}}'
check_assertion 0 8 assert delayed_success 'Transaction signature: example'
check_assertion 1 8 assert_fail unexpected_success "$CONFIRMATION_RESPONSE" 'Transaction signature: example'

CONFIRMATION_RESPONSE='{"result":{"meta":{"err":{"InstructionError":[0,{"Custom":1032}]}}}}'
check_assertion 1 8 assert execution_failure 'Transaction signature: example'
check_assertion 0 8 assert_fail expected_failure "$CONFIRMATION_RESPONSE" 'Transaction signature: example'
check_assertion 1 8 assert_fail wrong_failure 'different error' 'Transaction signature: example'

CONFIRMATION_RESPONSE='{"result":null}'
check_assertion 1 10 assert missing_transaction 'Transaction signature: example'
check_assertion 1 10 assert_fail missing_transaction "$CONFIRMATION_RESPONSE" 'Transaction signature: example'

# Simulation errors still use the existing exact-output comparison without RPC.
check_assertion 0 0 assert_fail simulation_failure 'simulation error' 'simulation error'

echo 'PASS: transaction confirmation framework regression tests'
