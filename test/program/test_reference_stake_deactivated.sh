#!/bin/bash

# Tests of MeasureRewardRate when the reference stake account has been deactivated: it must measure nothing, discard
# its snapshot, and delegate the reference stake account to Shinobi Systems again once the stake program allows it.
#
# Usage: SOURCE=<root of the Shinobi Immortals source> test/program/test_reference_stake_deactivated.sh
#
# This runs on its own test validator, separately from test/program/test.sh, because it needs a Shinobi Systems vote
# account that never votes, so that the stake program's DeactivateDelinquent instruction can deactivate the reference
# stake account (that is the only way that it can ever be deactivated).  The program is built (via
# SEPARATE_SHINOBI_SYSTEMS_VOTE, see setup in test/program/common) to treat a new, non-voting vote account as Shinobi
# Systems; the test validator's own vote account, which votes, serves as DeactivateDelinquent's reference vote account.
#
# The tests run in order and depend on each other.

set -e

if [ -z "$SOURCE" ]; then
    echo "The SOURCE variable must be set to the root directory of the Shinobi Immortals source"
    exit 1
fi

# Run every test; the helpers in test/program/common consult TESTS
TESTS=

source $SOURCE/test/program/common

# Build the program with a separate (non-voting) Shinobi Systems vote account; see the comments at the top
export SEPARATE_SHINOBI_SYSTEMS_VOTE=1

setup


# ---------------------------------------------------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------------------------------------------------

# Fails the test named $1 with the message $2
function fail_test ()
{
    echo "FAIL: $1: $2"
    exit 1
}


# Prints the value of a field of the JSON description of the reference stake account, or "null" if it is not present
function reference_field ()
{
    solana -u l stake-account $REFERENCE_STAKE_PUBKEY --output json | jq -r ".$1"
}


# Prints the snapshot stake of the program state (a u64 at byte offset 48)
function snapshot_stake ()
{
    get_account_data $STATE_PUBKEY 48 8 | base64 -d | od -An -tu8 -v | tr -d '[:space:]'
}


# Runs MeasureRewardRate, signed and paid for by rich_user1, printing the output of the submission.  $1, if given, is
# "require_measured".
function measure_reward_rate ()
{
    SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/anyone_measure_reward_rate_tx.sh $RICH_USER1_PUBKEY $1  \
        | solxact hash l                                                                                             \
        | solxact sign $LEDGER/rich_user1.json                                                                       \
        | solxact submit l 2>&1
}


# Waits until the current epoch is at least $2, failing test $1 if that takes more than two minutes
function wait_for_epoch ()
{
    local WAITED=0
    while [ `current_epoch` -lt $2 ]; do
        if [ $WAITED -ge 120 ]; then
            fail_test $1 "epoch $2 did not arrive"
        fi
        sleep 1
        WAITED=$((WAITED + 1))
    done
}


# ---------------------------------------------------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------------------------------------------------

# Initialize the program, which delegates the reference stake account to the (non-voting) Shinobi Systems vote account
assert reference_stake_setup_initialize                                                                              \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY SHINOBI_SYSTEMS_VOTE_PUBKEY=$SHINOBI_SYSTEMS_VOTE_PUBKEY                   \
     $SOURCE/scripts/super_initialize_tx.sh $SUPERUSER_PUBKEY $ADMIN_PUBKEY 490                                      \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/super.json                                                                                \
    | solxact submit l 2>&1`
if [ "`reference_field delegatedVoteAccountAddress`" != "$SHINOBI_SYSTEMS_VOTE_PUBKEY" ]; then
    fail_test reference_stake_setup_initialize "the reference stake account is not delegated to Shinobi Systems"
fi


# Run MeasureRewardRate until it has taken a snapshot, so that there is one for the deactivation to discard.  (The
# reference stake account earns nothing, since Shinobi Systems never votes, but it is delegated there, and so counts
# as earning.)  It does nothing at all until the level credit window in which Initialize set the reward rate is over,
# which can take up to LEVEL_CREDIT_WINDOW_SECONDS.
TEST_NAME=reference_stake_snapshot
wait_for_epoch $TEST_NAME $((`reference_field activationEpoch` + 1))
for ATTEMPT in `seq 1 $((LEVEL_CREDIT_WINDOW_SECONDS + 30))`; do
    assert ${TEST_NAME}_$ATTEMPT "`measure_reward_rate`"
    if [ "`snapshot_stake`" != 0 ]; then
        break
    fi
    sleep 1
done
if [ "`snapshot_stake`" = 0 ]; then
    fail_test $TEST_NAME "no snapshot was taken"
fi


# Deactivate the reference stake account with DeactivateDelinquent, as anyone may once Shinobi Systems has not voted
# for five epochs (and the reference vote account has voted in each of them).  Keep trying until the epochs have
# passed.
TEST_NAME=reference_stake_deactivate_delinquent
DEACTIVATED=
for ATTEMPT in `seq 1 120`; do
    if solana_stake -u l deactivate-stake --delinquent -k $LEDGER/rich_user1.json $REFERENCE_STAKE_PUBKEY            \
           >/dev/null 2>&1; then
        DEACTIVATED=1
        break
    fi
    sleep 1
done
if [ -z "$DEACTIVATED" ]; then
    fail_test $TEST_NAME "DeactivateDelinquent did not succeed"
fi
DEACTIVATION_EPOCH=`reference_field deactivationEpoch`
echo "+ $TEST_NAME (in epoch $DEACTIVATION_EPOCH)"


# Wait until the reference stake account has certainly finished cooling down, and its deactivation can no longer be
# rescinded (which the stake program allows only in the epoch of the deactivation): MeasureRewardRate will then
# delegate it afresh
wait_for_epoch reference_stake_cooled_down $((DEACTIVATION_EPOCH + 2))


# With require_measured, MeasureRewardRate fails, since it cannot measure; and the failure undoes everything else
# that it would have done, so the reference stake account stays deactivated and the snapshot stays.  0x416 = 1046 =
# Error_RewardRateNotMeasured.  (If the epoch's rewards are still being paid, it fails the same way.)
TEST_NAME=reference_stake_require_measured_fails
SNAPSHOT_BEFORE=`snapshot_stake`
assert_fail $TEST_NAME                                                                                               \
'Transaction simulation failed: Error processing Instruction 0: custom program error: 0x416'                         \
`measure_reward_rate require_measured`
if [ "`reference_field deactivationEpoch`" != "$DEACTIVATION_EPOCH" ]; then
    fail_test $TEST_NAME "the reference stake account was delegated again by a failed instruction"
fi
if [ "`snapshot_stake`" != "$SNAPSHOT_BEFORE" ]; then
    fail_test $TEST_NAME "the snapshot was changed by a failed instruction"
fi


# Without require_measured, MeasureRewardRate discards the snapshot and delegates the reference stake account to
# Shinobi Systems again.  Run it until it has (it does nothing while the epoch's rewards are being paid).
TEST_NAME=reference_stake_delegated_again
for ATTEMPT in `seq 1 30`; do
    assert ${TEST_NAME}_$ATTEMPT "`measure_reward_rate`"
    if [ "`reference_field deactivationEpoch`" = "null" ]; then
        break
    fi
    sleep 1
done
if [ "`reference_field deactivationEpoch`" != "null" ]; then
    fail_test $TEST_NAME "the reference stake account was not delegated again"
fi
if [ "`reference_field delegatedVoteAccountAddress`" != "$SHINOBI_SYSTEMS_VOTE_PUBKEY" ]; then
    fail_test $TEST_NAME "the reference stake account was delegated to `reference_field delegatedVoteAccountAddress`"
fi
ACTIVATION_EPOCH=`reference_field activationEpoch`
if [ "$ACTIVATION_EPOCH" -le "$DEACTIVATION_EPOCH" ]; then
    fail_test $TEST_NAME "the delegation was not new (activation epoch $ACTIVATION_EPOCH)"
fi
if [ "`snapshot_stake`" != 0 ]; then
    fail_test $TEST_NAME "the snapshot was not discarded"
fi
echo "+ $TEST_NAME (activation epoch $ACTIVATION_EPOCH)"


# Once the new delegation is earning (from the epoch after its activation), MeasureRewardRate takes a new snapshot,
# never one from before then
TEST_NAME=reference_stake_new_snapshot
for ATTEMPT in `seq 1 60`; do
    assert ${TEST_NAME}_$ATTEMPT "`measure_reward_rate`"
    if [ "`snapshot_stake`" != 0 ]; then
        break
    fi
    sleep 1
done
if [ "`snapshot_stake`" = 0 ]; then
    fail_test $TEST_NAME "no snapshot was taken"
fi
SNAPSHOT_EPOCH=`get_account_data $STATE_PUBKEY 56 8 | base64 -d | od -An -tu8 -v | tr -d '[:space:]'`
if [ "$SNAPSHOT_EPOCH" -le "$ACTIVATION_EPOCH" ]; then
    fail_test $TEST_NAME "the snapshot was taken in epoch $SNAPSHOT_EPOCH, before the delegation earned"
fi
if [ "`snapshot_stake`" != "`reference_field delegatedStake`" ]; then
    fail_test $TEST_NAME "the snapshot is not of the reference stake account's delegated stake"
fi
echo "+ $TEST_NAME (epoch $SNAPSHOT_EPOCH)"


echo "All reference_stake_deactivated tests passed"
