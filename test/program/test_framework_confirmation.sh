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

CONFIRMATION_RESPONSE='{"result":{"meta":{"err":{"InstructionError":[0,{"Custom":1024}]}}}}'
check_assertion 1 8 assert execution_failure 'Transaction signature: example'
check_assertion 0 8 assert_fail expected_failure "$CONFIRMATION_RESPONSE" 'Transaction signature: example'
check_assertion 1 8 assert_fail wrong_failure 'different error' 'Transaction signature: example'

CONFIRMATION_RESPONSE='{"result":null}'
check_assertion 1 $TRANSACTION_POLL_ATTEMPTS assert missing_transaction 'Transaction signature: example'
check_assertion 1 $TRANSACTION_POLL_ATTEMPTS assert_fail missing_transaction "$CONFIRMATION_RESPONSE" 'Transaction signature: example'

# Simulation errors still use the existing exact-output comparison without RPC.
check_assertion 0 0 assert_fail simulation_failure 'simulation error' 'simulation error'


# -----------------------------------------------------------------------------------------------------------------
# Regression tests for the solxact wrapper in common, which resubmits transactions that the stake program rejects
# with EpochRewardsActive.  A fake solxact is put first on the PATH, so that the wrapper's "command solxact" runs it.
# -----------------------------------------------------------------------------------------------------------------

# The wrapper keeps its signing records under $LEDGER
export LEDGER=$CONFIRMATION_TMP/ledger
mkdir $LEDGER

# The fake solxact.  "hash" and "sign" append a marker to the transaction, so that what was finally submitted shows
# how it was built.  "submit" saves each transaction it is given, and acts out the next line of $FAKE/responses:
#   sim-reject    fail simulation with the stake program's EpochRewardsActive
#   sim-other     fail simulation with error 0x10 from a program other than the stake program
#   sent SIG      pass simulation and print SIG as the signature of the sent transaction
FAKE=$CONFIRMATION_TMP/fake
mkdir -p $FAKE/bin
cat > $FAKE/bin/solxact <<'EOF'
#!/bin/bash
case "$1" in
    hash)
        echo "`cat`+hash"
        ;;
    sign)
        shift
        echo "`cat`+sign:$*"
        ;;
    submit)
        n=`ls $FAKE/submitted | wc -l`
        cat > $FAKE/submitted/$((n + 1))
        set -- `sed -n "$((n + 1))p" $FAKE/responses`
        case "$1" in
            sim-reject)
                echo "ERROR: {\"logs\":[\"Program Stake11111111111111111111111111111111111111 failed: custom program error: 0x10\"]}" >&2
                exit 255
                ;;
            sim-other)
                echo "ERROR: {\"logs\":[\"Program Other111 failed: custom program error: 0x10\"]}" >&2
                exit 255
                ;;
            sent)
                echo "Transaction signature: $2"
                ;;
        esac
        ;;
esac
EOF
chmod +x $FAKE/bin/solxact
export FAKE
PATH=$FAKE/bin:$PATH

# For these tests, the RPC node reports that a transaction with signature "rejected..." failed on chain with
# EpochRewardsActive, and that any other transaction succeeded
function curl ()
{
    if [[ "$*" = *'"rejected'* ]]; then
        echo '{"result":{"meta":{"err":{"InstructionError":[0,{"Custom":16}]},"logMessages":["Program Stake11111111111111111111111111111111111111 failed: custom program error: 0x10"]}}}'
    else
        echo '{"result":{"meta":{"err":null,"logMessages":[]}}}'
    fi
}

# Runs the test pipeline once with the responses given as arguments, and checks the exit status, the output, the
# number of submissions, and the final transaction that was submitted
function check_resubmission ()
{
    local name=$1 expected_status=$2 expected_output=$3 expected_submissions=$4 expected_last=$5
    shift 5
    rm -rf $FAKE/submitted
    mkdir $FAKE/submitted
    printf '%s\n' "$@" > $FAKE/responses
    local status=0 output
    output=`echo tx | solxact hash l | solxact sign a.json | solxact sign b.json c.json | solxact submit l 2>&1`     \
        || status=$?
    local submissions=`ls $FAKE/submitted | wc -l`
    local last=`cat $FAKE/submitted/$submissions`
    if [ "$status" -ne "$expected_status" ] || [ "$output" != "$expected_output" ] ||
       [ "$submissions" -ne "$expected_submissions" ] || [ "$last" != "$expected_last" ]; then
        echo "FAIL: $name: status $status, output '$output', $submissions submissions, last submitted '$last'"
        exit 1
    fi
}

# The transaction as first built, and as rebuilt: a fresh blockhash applied to the transaction from before signing,
# signed again by all three signers at once
ORIGINAL='tx+hash+sign:a.json+sign:b.json c.json'
REBUILT='tx+hash+hash+sign:a.json b.json c.json'
REJECTION='ERROR: {"logs":["Program Stake11111111111111111111111111111111111111 failed: custom program error: 0x10"]}'

# A transaction that is not rejected is submitted once, unchanged
check_resubmission no_rejection 0 'Transaction signature: ok' 1 "$ORIGINAL" 'sent ok'

# Rejected twice in simulation, then rebuilt, re-signed by every signer and resubmitted until it is accepted
check_resubmission simulation_rejection 0 'Transaction signature: ok' 3 "$REBUILT" sim-reject sim-reject 'sent ok'

# Sent, but rejected on chain, then rebuilt and resubmitted
check_resubmission execution_rejection 0 'Transaction signature: ok' 2 "$REBUILT" 'sent rejected1' 'sent ok'

# The same error number from another program is not EpochRewardsActive, so it is reported and not retried
check_resubmission other_program_error 255 'ERROR: {"logs":["Program Other111 failed: custom program error: 0x10"]}' \
    1 "$ORIGINAL" sim-other

# Rejection that never ends is given up on after 60 attempts, reporting the final rejection
check_resubmission endless_rejection 255 "$REJECTION" 60 "$REBUILT" `for i in $(seq 1 60); do echo sim-reject; done`

echo 'PASS: transaction confirmation framework regression tests'
