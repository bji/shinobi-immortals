#!/bin/bash

# Tests of the recovery of the master stake account after it has been deactivated: DelegateMasterStake (21) and
# WithdrawExcess (22) of the master stake account.
#
# Usage: SOURCE=<root of the Shinobi Immortals source> test/test_master_stake_recovery.sh
#
# Nothing in the program ever deactivates the master stake account.  The one way that it can become deactivated is the
# stake program's DeactivateDelinquent instruction, which lets anyone deactivate a stake account that is delegated to
# a validator which has not voted for five epochs.
#
# This runs on its own test validator, separately from test/test.sh, because in the shared suite Shinobi Systems is
# the test validator itself, which never stops voting.  Here the program is built (via SEPARATE_SHINOBI_SYSTEMS_VOTE,
# see setup in test/common) to treat a new vote account that never votes as Shinobi Systems, which makes the master
# stake account eligible for DeactivateDelinquent.  The test validator's own vote account serves as the reference
# vote account which that instruction requires as proof that the cluster itself has been running.
#
# The tests run in order and depend on each other.

set -e

if [ -z "$SOURCE" ]; then
    echo "The SOURCE variable must be set to the root directory of the Shinobi Immortals source"
    exit 1
fi

# Run every test; the helpers in test/common consult TESTS
TESTS=

source $SOURCE/test/common

# Build the program with a separate (non-voting) Shinobi Systems vote account; see the comments at the top
export SEPARATE_SHINOBI_SYSTEMS_VOTE=1

setup


# ---------------------------------------------------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------------------------------------------------

# The lamports that the master stake account must always keep in addition to its rent exempt reserve:
# MASTER_STAKE_ACCOUNT_MIN_LAMPORTS in program/inc/constants.h
MASTER_MIN_LAMPORTS=2000000000


# Fails the test named $1 with the message $2
function fail_test ()
{
    echo "FAIL: $1: $2"
    exit 1
}


# Prints a field of the JSON description of the master stake account, or "null" if it is not present
function master_stake_field ()
{
    solana -u l stake-account $MASTER_STAKE_PUBKEY --output json | jq -r ".$1"
}


# Fails the test named $1 unless scripts/show.sh reports that the master stake account's actively_delegated is $2
# and that its withdrawable excess is $3 lamports.  show.sh reports amounts in SOL, so the excess is converted back
# to lamports.
function check_show_master_stake ()
{
    local JSON=`PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/show.sh -u l master_stake`
    local ACTIVELY_DELEGATED=`echo "$JSON" | jq -r ".actively_delegated"`
    local EXCESS=`echo "$JSON" | jq -r ".withdrawable_excess * 1000000000 | round"`

    if [ "$ACTIVELY_DELEGATED" != "$2" ] || [ "$EXCESS" != "$3" ]; then
        fail_test $1 "show.sh reports actively_delegated $ACTIVELY_DELEGATED and excess $EXCESS, expected $2 and $3"
    fi
}


# Prints the balance of account $1 in lamports
function lamports_of ()
{
    solana -u l balance --lamports $1 | cut -d ' ' -f 1
}


# Submits a DelegateMasterStake transaction paid for and signed by the key file $1, printing the result of submission
function delegate_master_stake ()
{
    SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY SHINOBI_SYSTEMS_VOTE_PUBKEY=$SHINOBI_SYSTEMS_VOTE_PUBKEY                \
        $SOURCE/scripts/anyone_delegate_master_stake_tx.sh `solxact pubkey $1`                                       \
        | solxact hash l                                                                                             \
        | solxact sign $1                                                                                            \
        | solxact submit l 2>&1
}


# Submits a WithdrawExcess transaction for the master stake account with the key file $1 as the admin, printing the
# result of submission
function withdraw_master_stake ()
{
    SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY                                                                         \
        $SOURCE/scripts/admin_withdraw_excess_tx.sh `solxact pubkey $1` master_stake                                 \
        | solxact hash l                                                                                             \
        | solxact sign $1                                                                                            \
        | solxact submit l 2>&1
}


# Checks that the master stake account holds exactly its rent exempt reserve plus its minimum; fails test $1 if not
function check_master_at_minimum ()
{
    local BALANCE=`master_stake_field accountBalance`
    local EXPECTED=$((`master_stake_field rentExemptReserve` + MASTER_MIN_LAMPORTS))

    if [ "$BALANCE" != "$EXPECTED" ]; then
        fail_test $1 "master stake account has $BALANCE lamports, expected $EXPECTED"
    fi
}


# ---------------------------------------------------------------------------------------------------------------------
# Setup
# ---------------------------------------------------------------------------------------------------------------------

# Initialize the program; this creates the master stake account with its minimum balance, and delegates it to the
# (separate, never voting) Shinobi Systems vote account
assert master_stake_recovery_setup_initialize                                                                        \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY SHINOBI_SYSTEMS_VOTE_PUBKEY=$SHINOBI_SYSTEMS_VOTE_PUBKEY                   \
     $SOURCE/scripts/super_initialize_tx.sh $SUPERUSER_PUBKEY $ADMIN_PUBKEY                                          \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/super.json                                                                                \
    | solxact submit l 2>&1`

# The master stake account starts out actively delegated to Shinobi Systems, holding exactly its minimum
TEST_NAME=master_stake_recovery_setup_state
if [ "`master_stake_field delegatedVoteAccountAddress`" != "$SHINOBI_SYSTEMS_VOTE_PUBKEY" ] ||
   [ "`master_stake_field deactivationEpoch`" != "null" ]; then
    fail_test $TEST_NAME "master stake account is not actively delegated to Shinobi Systems"
fi
check_master_at_minimum $TEST_NAME
# scripts/show.sh agrees, and reports nothing to withdraw
check_show_master_stake $TEST_NAME true 0


# ---------------------------------------------------------------------------------------------------------------------
# While the master stake account is actively delegated
# ---------------------------------------------------------------------------------------------------------------------

# DelegateMasterStake is refused, since there is nothing to do (Error_MasterStakeAlreadyDelegated, 1042)
assert_fail delegate_master_stake_while_active                                                                       \
'Transaction simulation failed: Error processing Instruction 0: custom program error: 0x412'                         \
`delegate_master_stake $LEDGER/rich_user2.json`

# WithdrawExcess is refused, since the account holds only its minimum (Error_NothingToWithdraw, 1043)
assert_fail withdraw_master_stake_while_active_at_minimum                                                            \
'Transaction simulation failed: Error processing Instruction 0: custom program error: 0x413'                         \
`withdraw_master_stake $LEDGER/admin.json`


# ---------------------------------------------------------------------------------------------------------------------
# Deactivate the master stake account
# ---------------------------------------------------------------------------------------------------------------------

# Deactivate the master stake account with the stake program's DeactivateDelinquent instruction, exactly as any
# member of the public could if Shinobi Systems stopped voting.  The stake program accepts this only once the test
# validator's vote account (the reference vote account, which the solana command finds for itself) has voted in each
# of the last five epochs, so keep trying for up to five minutes while the local cluster gets that far.
TEST_NAME=master_stake_deactivate_delinquent
echo "Deactivating the master stake account as delinquent"
WAITED=0
while ! solana_stake -u l deactivate-stake --delinquent -k $LEDGER/rich_user1.json $MASTER_STAKE_PUBKEY              \
            > $LEDGER/deactivate_delinquent.out 2>&1; do
    if [ $WAITED -ge 300 ]; then
        cat $LEDGER/deactivate_delinquent.out
        fail_test $TEST_NAME "could not deactivate the master stake account as delinquent"
    fi
    sleep 5
    WAITED=$((WAITED + 5))
done
if [ "`master_stake_field deactivationEpoch`" = "null" ]; then
    fail_test $TEST_NAME "master stake account was not deactivated"
fi
echo "+ $TEST_NAME"

# Wait for the master stake account to finish cooling down: until then the stake program does not allow it to be
# delegated again.  When none of its stake is active or deactivating any longer, the solana command omits those
# fields from its description.
echo "Waiting for the master stake account to cool down"
WAITED=0
while [ "`master_stake_field activeStake`" != "null" ] || [ "`master_stake_field deactivatingStake`" != "null" ]; do
    if [ $WAITED -ge 300 ]; then
        fail_test master_stake_cooldown "master stake account did not finish cooling down"
    fi
    sleep 5
    WAITED=$((WAITED + 5))
done


# ---------------------------------------------------------------------------------------------------------------------
# While the master stake account is deactivated
# ---------------------------------------------------------------------------------------------------------------------

# None of the master stake account's lamports are staked now, so the stake program itself would allow all of them
# other than the rent exempt reserve to be withdrawn.  The program must still refuse to take the account below its
# minimum.
assert_fail withdraw_master_stake_deactivated_at_minimum                                                             \
'Transaction simulation failed: Error processing Instruction 0: custom program error: 0x413'                         \
`withdraw_master_stake $LEDGER/admin.json`
check_master_at_minimum withdraw_master_stake_deactivated_at_minimum

# Put 2 SOL more into the master stake account, to stand for commission that had accumulated in it
transfer $LEDGER/rich_user1.json $MASTER_STAKE_PUBKEY 2

# scripts/show.sh reports that the account is no longer actively delegated, and that exactly those 2 SOL may be
# withdrawn
check_show_master_stake show_master_stake_deactivated false 2000000000

# Only the admin may withdraw (Error_PermissionDenied, 1004)
assert_fail withdraw_master_stake_deactivated_not_admin                                                              \
'Transaction simulation failed: Error processing Instruction 0: custom program error: 0x3ec'                         \
`withdraw_master_stake $LEDGER/rich_user2.json`

# The admin withdraws: exactly the 2 SOL above the minimum goes to the admin, who also pays the 5000 lamport
# transaction fee, and the minimum stays in the master stake account
TEST_NAME=withdraw_master_stake_deactivated
ADMIN_BEFORE=`lamports_of $ADMIN_PUBKEY`
assert $TEST_NAME                                                                                                    \
`withdraw_master_stake $LEDGER/admin.json`
ADMIN_AFTER=`lamports_of $ADMIN_PUBKEY`
if [ $((ADMIN_AFTER - ADMIN_BEFORE)) != $((2000000000 - 5000)) ]; then
    fail_test $TEST_NAME "admin balance changed by $((ADMIN_AFTER - ADMIN_BEFORE)), expected $((2000000000 - 5000))"
fi
check_master_at_minimum $TEST_NAME

# And now there is nothing more to withdraw
assert_fail withdraw_master_stake_deactivated_again                                                                  \
'Transaction simulation failed: Error processing Instruction 0: custom program error: 0x413'                         \
`withdraw_master_stake $LEDGER/admin.json`


# ---------------------------------------------------------------------------------------------------------------------
# Delegate the master stake account again
# ---------------------------------------------------------------------------------------------------------------------

# Anyone may delegate the deactivated master stake account to Shinobi Systems again; here it is rich_user2, who has
# no role in the program.  The minimum that the withdrawal left behind is all delegated.
TEST_NAME=delegate_master_stake_deactivated
assert $TEST_NAME                                                                                                    \
`delegate_master_stake $LEDGER/rich_user2.json`
if [ "`master_stake_field delegatedVoteAccountAddress`" != "$SHINOBI_SYSTEMS_VOTE_PUBKEY" ]; then
    fail_test $TEST_NAME "master stake account is not delegated to Shinobi Systems"
fi
if [ "`master_stake_field deactivationEpoch`" != "null" ]; then
    fail_test $TEST_NAME "master stake account is still deactivated"
fi
if [ "`master_stake_field delegatedStake`" != "$MASTER_MIN_LAMPORTS" ]; then
    fail_test $TEST_NAME "master stake account has `master_stake_field delegatedStake` lamports delegated,"          \
              "expected $MASTER_MIN_LAMPORTS"
fi
if [ "`master_stake_field staker`" != "$AUTHORITY_PUBKEY" ] ||
   [ "`master_stake_field withdrawer`" != "$AUTHORITY_PUBKEY" ]; then
    fail_test $TEST_NAME "master stake account authorities changed"
fi
check_master_at_minimum $TEST_NAME
# scripts/show.sh reports it as actively delegated again, with nothing to withdraw
check_show_master_stake $TEST_NAME true 0

# It is actively delegated again, so a second DelegateMasterStake is refused
assert_fail delegate_master_stake_again                                                                              \
'Transaction simulation failed: Error processing Instruction 0: custom program error: 0x412'                         \
`delegate_master_stake $LEDGER/rich_user2.json`

# And its delegated stake cannot be withdrawn
assert_fail withdraw_master_stake_after_delegate                                                                     \
'Transaction simulation failed: Error processing Instruction 0: custom program error: 0x413'                         \
`withdraw_master_stake $LEDGER/admin.json`


echo "All master_stake_recovery tests passed"
