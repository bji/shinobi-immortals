#!/bin/bash

# Tests for current_issues.txt issue 6: destaking, and harvesting from, a stake account that Stake deactivated
# because it was delegated to a validator other than Shinobi Systems.
#
# Usage: SOURCE=<root of the Shinobi Immortals source> test/test_destake_deactivated_elsewhere.sh
#
# This runs on its own test validator, separately from test/test.sh, because it needs something the shared suite
# cannot provide: a validator other than Shinobi Systems that pays staking rewards.  The test validator is the only
# validator on the local cluster that votes, so it is the only one whose delegators earn rewards.  Here the program is
# built (via SEPARATE_SHINOBI_SYSTEMS_VOTE, see setup in test/common) to treat a new, non-voting vote account as
# Shinobi Systems, and the test validator plays the part of the other validator.
#
# The situation under test is the one that issue 6 describes: a user stakes an entry with a stake account that is
# active at another validator.  Stake deactivates it, and records the entry's Ki baseline from its delegated stake.
# The account still earns that validator's rewards for the epoch in which it was deactivated, so at the next epoch its
# delegated stake is above the baseline.  Harvest must not mint Ki on those rewards, and Destake must return the stake
# account untouched: no Ki is earned from a deactivated or activating stake account.
#
# Each test checks a precondition showing that the account really does hold rewards that the program would have
# minted Ki on without the fix, so that the tests cannot pass vacuously.
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

# The vote account that the user's stake accounts are delegated to before being staked: the test validator's, which
# from the program's point of view is a validator other than Shinobi Systems
OTHER_VOTE_PUBKEY=$VOTE_PUBKEY


# ---------------------------------------------------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------------------------------------------------

# Fails the test named $1 with the message $2
function fail_test ()
{
    echo "FAIL: $1: $2"
    exit 1
}


# Prints the value of a field of the JSON description of a stake account, or "null" if it is not present
function stake_field ()
{
    local STAKE_PUBKEY=$1
    local FIELD=$2

    solana -u l stake-account $STAKE_PUBKEY --output json | jq -r ".$FIELD"
}


# Prints a u64 value stored at a byte offset within an account's data
function account_u64 ()
{
    local ACCOUNT_PUBKEY=$1
    local OFFSET=$2

    get_account_data $ACCOUNT_PUBKEY $OFFSET 8 | base64 -d | od -An -tu8 -v | tr -d '[:space:]'
}


# Byte offsets within Entry (program/inc/entry.h) of the owned struct, which is 40 bytes long, and of its
# last_ki_harvest_stake_account_lamports field
ENTRY_OWNED_OFFSET=264
ENTRY_OWNED_SIZE=40
ENTRY_KI_BASELINE_OFFSET=296


# Prints the entry account address of entry $1 of block 1 0, the block that these tests use
function test_entry ()
{
    local BLOCK_PUBKEY=`pda $SELF_PROGRAM_PUBKEY [ u8 10 u32 1 u32 0 ]`
    local MINT_PUBKEY=`pda $SELF_PROGRAM_PUBKEY [ u8 4 pubkey $BLOCK_PUBKEY u16 $1 ]`
    pda $SELF_PROGRAM_PUBKEY [ u8 11 pubkey $MINT_PUBKEY ]
}


# expected_ki_mint, used below, is in test/common


# Waits up to 60 seconds for the delegated stake of stake account $2 to rise above entry $3's Ki baseline, which
# happens when the epoch's staking rewards are paid (rewards are distributed over the first slots of an epoch, not
# all at once at its boundary); fails test $1 if it does not
function wait_for_rewards_above_baseline ()
{
    local TEST_NAME=$1
    local STAKE_PUBKEY=$2
    local ENTRY_PUBKEY=$3

    local BASELINE=`account_u64 $ENTRY_PUBKEY $ENTRY_KI_BASELINE_OFFSET`
    local WAITED=0
    echo "Waiting for rewards to be paid to $STAKE_PUBKEY"
    while [ `stake_field $STAKE_PUBKEY delegatedStake` -le $BASELINE ]; do
        if [ $WAITED -ge 60 ]; then
            fail_test $TEST_NAME "no rewards were paid to $STAKE_PUBKEY above the baseline $BASELINE"
        fi
        sleep 1
        WAITED=$((WAITED + 1))
    done
}


# Checks that stake account $2 is delegated to vote account $3, has staker and withdrawer authority $4, and has
# deactivation state $5 ("deactivated" or "active"); fails test $1 if not
function check_stake ()
{
    local TEST_NAME=$1
    local STAKE_PUBKEY=$2
    local EXPECTED_VOTE_PUBKEY=$3
    local EXPECTED_AUTHORITY=$4
    local EXPECTED_STATE=$5

    local JSON=`solana -u l stake-account $STAKE_PUBKEY --output json`
    local VOTER=`echo "$JSON" | jq -r .delegatedVoteAccountAddress`
    local DEACTIVATION_EPOCH=`echo "$JSON" | jq -r .deactivationEpoch`

    if [ "$VOTER" != "$EXPECTED_VOTE_PUBKEY" ]; then
        fail_test $TEST_NAME "stake account delegated to $VOTER, expected $EXPECTED_VOTE_PUBKEY"
    fi
    if [ "`echo "$JSON" | jq -r .staker`" != "$EXPECTED_AUTHORITY" ] ||
       [ "`echo "$JSON" | jq -r .withdrawer`" != "$EXPECTED_AUTHORITY" ]; then
        fail_test $TEST_NAME "stake account authorities are not $EXPECTED_AUTHORITY"
    fi
    if [ "$EXPECTED_STATE" = "deactivated" ] && [ "$DEACTIVATION_EPOCH" = "null" ]; then
        fail_test $TEST_NAME "stake account is not deactivated"
    fi
    if [ "$EXPECTED_STATE" = "active" ] && [ "$DEACTIVATION_EPOCH" != "null" ]; then
        fail_test $TEST_NAME "stake account is deactivated (epoch $DEACTIVATION_EPOCH)"
    fi
}


# Prints a one-line snapshot of everything that a harvest or destake must leave unchanged other than the entry's
# owned struct and the stake account's authorities: the stake account's delegated stake and lamports, and
# rich_user1's Ki balance
function balances_snapshot ()
{
    local STAKE_PUBKEY=$1

    echo "stake `stake_field $STAKE_PUBKEY delegatedStake` `stake_field $STAKE_PUBKEY accountBalance`"                \
         "ki `get_token_balance $KI_MINT_PUBKEY $RICH_USER1_PUBKEY`"
}


# Prints the entry's owned struct, base64 encoded
function entry_owned ()
{
    get_account_data $1 $ENTRY_OWNED_OFFSET $ENTRY_OWNED_SIZE
}


# Checks the preconditions that make a test of stake account $2, staked in entry $3, meaningful: its delegated stake
# is above the entry's Ki baseline by enough that, without the fix, the program would mint a nonzero amount of Ki.
# Fails test $1 if not.
function check_rewards_owed ()
{
    local TEST_NAME=$1
    local STAKE_PUBKEY=$2
    local ENTRY_PUBKEY=$3

    local DELEGATED=`stake_field $STAKE_PUBKEY delegatedStake`
    local KI_BASELINE=`account_u64 $ENTRY_PUBKEY $ENTRY_KI_BASELINE_OFFSET`

    local KI=0
    if [ $DELEGATED -gt $KI_BASELINE ]; then
        KI=`expected_ki_mint $((DELEGATED - KI_BASELINE)) $DELEGATED $KI_FACTOR`
    fi
    if [ $KI -le 0 ]; then
        fail_test $TEST_NAME "precondition: no Ki would be minted (stake $DELEGATED, baseline $KI_BASELINE)"
    fi
    echo "$TEST_NAME: without the fix, $KI Ki base units would be minted"
}


# Destakes entry $2 with stake account $3, and checks that the destake succeeded and did nothing other than return
# the stake account to rich_user1 and clear the entry's owned struct: no stake moved, and no Ki minted.  The stake account must still be delegated to vote account $4.  Fails test $1 if not.
function destake_and_check ()
{
    local TEST_NAME=$1
    local ENTRY_INDEX=$2
    local STAKE_PUBKEY=$3
    local EXPECTED_VOTE_PUBKEY=$4
    local ENTRY_PUBKEY=`test_entry $ENTRY_INDEX`

    local BEFORE=`balances_snapshot $STAKE_PUBKEY`
    assert $TEST_NAME                                                                                                \
    `SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/user_destake_tx.sh                                     \
         $RICH_USER1_PUBKEY 1 0 $ENTRY_INDEX $STAKE_PUBKEY                                                           \
        | solxact hash l                                                                                             \
        | solxact sign $LEDGER/rich_user1.json                                                                       \
        | solxact submit l 2>&1`
    local AFTER=`balances_snapshot $STAKE_PUBKEY`

    # Nothing moved: nothing out of the stake account, and no Ki minted
    if [ "$BEFORE" != "$AFTER" ]; then
        fail_test $TEST_NAME "balances changed: before [$BEFORE] after [$AFTER]"
    fi
    # The stake account belongs to the user again, and is otherwise as it was
    check_stake $TEST_NAME $STAKE_PUBKEY $EXPECTED_VOTE_PUBKEY $RICH_USER1_PUBKEY $5
    # The entry is no longer staked: its owned struct is all zeroes
    if [ "`entry_owned $ENTRY_PUBKEY | base64 -d | od -An -tx1 -v | tr -d ' 0\n'`" != "" ]; then
        fail_test $TEST_NAME "entry's owned fields were not cleared"
    fi
}


# ---------------------------------------------------------------------------------------------------------------------
# Setup
# ---------------------------------------------------------------------------------------------------------------------

# The entries' level 0 ki_factor.  The rewards paid to a 1000 SOL stake account in one epoch of the local cluster are
# small, so a large factor is used to ensure that, without the fix, harvesting them would mint a nonzero amount of Ki.
# 1000000000 Ki per SOL is one Ki per effective lamport.  It is written little-endian: 0x3B9ACA00.
KI_FACTOR=1000000000

# Entry metadata; only the level 0 ki_factor, at byte offset 76, is nonzero
METADATA=`(dd if=/dev/zero bs=1 count=76 status=none; echo 00ca9a3b | xxd -r -p; dd if=/dev/zero bs=1 count=2764 status=none) | base64 | tr -d '\n'`
# Metadata to set into entries, only needs to include up to the ki_factor
METADATA_HEAD=`(dd if=/dev/zero bs=1 count=76 status=none; echo 00ca9a3b | xxd -r -p) | base64 | tr -d '\n'`
SALT0=0
SALT1=1
SALT2=2
SHA2560=`compute_metadata_sha256 $METADATA $SALT0`
SHA2561=`compute_metadata_sha256 $METADATA $SALT1`
SHA2562=`compute_metadata_sha256 $METADATA $SALT2`

# Initialize the program
assert destake_deactivated_setup_initialize                                                                          \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY SHINOBI_SYSTEMS_VOTE_PUBKEY=$SHINOBI_SYSTEMS_VOTE_PUBKEY                   \
     $SOURCE/scripts/super_initialize_tx.sh $SUPERUSER_PUBKEY $ADMIN_PUBKEY                                          \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/super.json                                                                                \
    | solxact submit l 2>&1`

# Create three stake accounts owned by rich_user1, delegated to the other validator (the test validator):
# - elsewhere_stake_a: tests destaking after Stake deactivates it, without being redelegated
# - elsewhere_stake_b: tests destaking after the crank redelegates it to Shinobi Systems
# - elsewhere_stake_c: tests destaking in the same epoch that Stake deactivates it
for i in a b c; do
    make_stake_account $LEDGER/rich_user1.json $LEDGER/elsewhere_stake_$i.json 1000
    echo "Delegating $LEDGER/elsewhere_stake_$i.json to the other validator"
    solana_stake -u l delegate-stake -k $LEDGER/rich_user1.json $LEDGER/elsewhere_stake_$i.json $OTHER_VOTE_PUBKEY    \
           >/dev/null 2>/dev/null
done
ELSEWHERE_STAKE_A_PUBKEY=`solxact pubkey $LEDGER/elsewhere_stake_a.json`
ELSEWHERE_STAKE_B_PUBKEY=`solxact pubkey $LEDGER/elsewhere_stake_b.json`
ELSEWHERE_STAKE_C_PUBKEY=`solxact pubkey $LEDGER/elsewhere_stake_c.json`

# Create block 1 0: complete and revealed, with three entries
assert destake_deactivated_setup_create_block                                                                        \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/admin_create_block_tx.sh                                   \
     $ADMIN_PUBKEY 1 0 3 0 $((24*60*60)) \`lamports_from_sol 1000\` 1                                                \
     \`lamports_from_sol 1\` false $((24*60*60)) \`lamports_from_sol 1000\` 0                                        \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/admin.json                                                                                \
    | solxact submit l 2>&1`
# Add the entries two then one, since adding three in one transaction exceeds the compute limit
assert destake_deactivated_setup_add_entries_a                                                                       \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/admin_add_entries_to_block_tx.sh                           \
     $ADMIN_PUBKEY 1 0 "http://foo.bar.com" none 0 $SHA2560 $SHA2561                                                 \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/admin.json                                                                                \
    | solxact submit l 2>&1`
assert destake_deactivated_setup_add_entries_b                                                                       \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/admin_add_entries_to_block_tx.sh                           \
     $ADMIN_PUBKEY 1 0 "http://foo.bar.com" none 2 $SHA2562                                                          \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/admin.json                                                                                \
    | solxact submit l 2>&1`
# Set the metadata of each entry
for i in 0 1 2; do
    assert destake_deactivated_setup_set_metadata_$i                                                                 \
    `SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/admin_set_metadata_bytes_tx.sh                         \
         $ADMIN_PUBKEY 1 0 $i 0 $METADATA_HEAD                                                                       \
        | solxact hash l                                                                                             \
        | solxact sign $LEDGER/admin.json                                                                            \
        | solxact submit l 2>&1`
done
# Reveal the entries
assert destake_deactivated_setup_reveal                                                                              \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/admin_reveal_entries_tx.sh                                 \
     $ADMIN_PUBKEY 1 0 0 $SALT0 $SALT1 $SALT2                                                                        \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/admin.json                                                                                \
    | solxact submit l 2>&1`
# rich_user1 buys each entry
for i in 0 1 2; do
    assert destake_deactivated_setup_buy_$i                                                                          \
    `SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/user_buy_tx.sh                                         \
         $ADMIN_PUBKEY $RICH_USER1_PUBKEY 1 0 $i \`lamports_from_sol 1001\`                                          \
        | solxact hash l                                                                                             \
        | solxact sign $LEDGER/rich_user1.json                                                                       \
        | solxact submit l 2>&1`
done

# Wait until the next epoch, so that the stake accounts are fully active at the other validator
sleep_until_next_epoch

# Stake entries 0 and 1 with elsewhere_stake_a and elsewhere_stake_b.  Both are active at the other validator, so
# Stake takes over their authorities and deactivates them, and sets the entries' Ki baselines to their delegated
# stake.
for i in 0 1; do
    if [ $i = 0 ]; then STAKE_PUBKEY=$ELSEWHERE_STAKE_A_PUBKEY; else STAKE_PUBKEY=$ELSEWHERE_STAKE_B_PUBKEY; fi
    assert destake_deactivated_setup_stake_$i                                                                        \
    `SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/user_stake_tx.sh                                       \
         $RICH_USER1_PUBKEY 1 0 $i $STAKE_PUBKEY                                                                     \
        | solxact hash l                                                                                             \
        | solxact sign $LEDGER/rich_user1.json                                                                       \
        | solxact submit l 2>&1`
    check_stake destake_deactivated_setup_stake_$i $STAKE_PUBKEY $OTHER_VOTE_PUBKEY $AUTHORITY_PUBKEY deactivated
done


# ---------------------------------------------------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------------------------------------------------

# Destake in the same epoch in which Stake deactivated the stake account, while it is still deactivating.  No rewards
# have been paid since staking, so this mostly checks that destaking a deactivating account works at all.
TEST_NAME=destake_deactivated_same_epoch
assert ${TEST_NAME}_stake                                                                                            \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/user_stake_tx.sh                                           \
     $RICH_USER1_PUBKEY 1 0 2 $ELSEWHERE_STAKE_C_PUBKEY                                                              \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/rich_user1.json                                                                           \
    | solxact submit l 2>&1`
check_stake ${TEST_NAME}_stake $ELSEWHERE_STAKE_C_PUBKEY $OTHER_VOTE_PUBKEY $AUTHORITY_PUBKEY deactivated
destake_and_check $TEST_NAME 2 $ELSEWHERE_STAKE_C_PUBKEY $OTHER_VOTE_PUBKEY deactivated


# Wait for the epoch to end.  elsewhere_stake_a and elsewhere_stake_b are now fully inactive, and have been paid the
# other validator's rewards for the epoch in which they were deactivated, which raises their delegated stake above the
# entries' Ki baselines.
sleep_until_next_epoch
wait_for_rewards_above_baseline destake_deactivated_rewards_a $ELSEWHERE_STAKE_A_PUBKEY `test_entry 0`
wait_for_rewards_above_baseline destake_deactivated_rewards_b $ELSEWHERE_STAKE_B_PUBKEY `test_entry 1`


# Harvest from the deactivated stake account: it must succeed, mint no Ki, and leave the entry's Ki baseline unchanged
# (so that the other validator's rewards are never counted, even after the account is redelegated, which resets it)
TEST_NAME=harvest_deactivated_elsewhere
ENTRY_PUBKEY=`test_entry 0`
check_rewards_owed $TEST_NAME $ELSEWHERE_STAKE_A_PUBKEY $ENTRY_PUBKEY
BEFORE=`balances_snapshot $ELSEWHERE_STAKE_A_PUBKEY`
OWNED_BEFORE=`entry_owned $ENTRY_PUBKEY`
assert $TEST_NAME                                                                                                    \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/user_harvest_tx.sh                                         \
     $RICH_USER1_PUBKEY 1 0 0 $ELSEWHERE_STAKE_A_PUBKEY                                                              \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/rich_user1.json                                                                           \
    | solxact submit l 2>&1`
AFTER=`balances_snapshot $ELSEWHERE_STAKE_A_PUBKEY`
if [ "$BEFORE" != "$AFTER" ]; then
    fail_test $TEST_NAME "balances changed: before [$BEFORE] after [$AFTER]"
fi
if [ "$OWNED_BEFORE" != "`entry_owned $ENTRY_PUBKEY`" ]; then
    fail_test $TEST_NAME "entry's owned fields (including the Ki baseline) changed"
fi
check_stake $TEST_NAME $ELSEWHERE_STAKE_A_PUBKEY $OTHER_VOTE_PUBKEY $AUTHORITY_PUBKEY deactivated


# Destake the deactivated stake account before the crank has redelegated it.  This is the case that issue 6 describes:
# the user must get their stake account back, untouched, and earn no Ki on the other validator's rewards.
TEST_NAME=destake_deactivated_before_redelegate
check_rewards_owed $TEST_NAME $ELSEWHERE_STAKE_A_PUBKEY `test_entry 0`
destake_and_check $TEST_NAME 0 $ELSEWHERE_STAKE_A_PUBKEY $OTHER_VOTE_PUBKEY deactivated


# The crank redelegates the deactivated stake account to Shinobi Systems.  The entry's Ki baseline must be reset to
# the new delegated stake, which includes the other validator's rewards, so that those rewards never earn Ki.
TEST_NAME=destake_deactivated_redelegate
ENTRY_PUBKEY=`test_entry 1`
check_rewards_owed $TEST_NAME $ELSEWHERE_STAKE_B_PUBKEY $ENTRY_PUBKEY
# rich_user2 runs the crank, to show that anyone can
assert $TEST_NAME                                                                                                    \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/anyone_delegate_tx.sh                                      \
     $RICH_USER2_PUBKEY 1 0 1 $ELSEWHERE_STAKE_B_PUBKEY                                                              \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/rich_user2.json                                                                           \
    | solxact submit l 2>&1`
check_stake $TEST_NAME $ELSEWHERE_STAKE_B_PUBKEY $SHINOBI_SYSTEMS_VOTE_PUBKEY $AUTHORITY_PUBKEY active
DELEGATED=`stake_field $ELSEWHERE_STAKE_B_PUBKEY delegatedStake`
if [ "`account_u64 $ENTRY_PUBKEY $ENTRY_KI_BASELINE_OFFSET`" != "$DELEGATED" ]; then
    fail_test $TEST_NAME "Ki baseline is `account_u64 $ENTRY_PUBKEY $ENTRY_KI_BASELINE_OFFSET`, expected $DELEGATED"
fi


# Harvest from the redelegated stake account while it is activating: it must succeed and mint nothing
TEST_NAME=harvest_redelegated_activating
BEFORE=`balances_snapshot $ELSEWHERE_STAKE_B_PUBKEY`
OWNED_BEFORE=`entry_owned $ENTRY_PUBKEY`
assert $TEST_NAME                                                                                                    \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/user_harvest_tx.sh                                         \
     $RICH_USER1_PUBKEY 1 0 1 $ELSEWHERE_STAKE_B_PUBKEY                                                              \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/rich_user1.json                                                                           \
    | solxact submit l 2>&1`
AFTER=`balances_snapshot $ELSEWHERE_STAKE_B_PUBKEY`
if [ "$BEFORE" != "$AFTER" ]; then
    fail_test $TEST_NAME "balances changed: before [$BEFORE] after [$AFTER]"
fi
if [ "$OWNED_BEFORE" != "`entry_owned $ENTRY_PUBKEY`" ]; then
    fail_test $TEST_NAME "entry's owned fields (including the Ki baseline) changed"
fi


# Destake the redelegated stake account in its activation epoch.  It must succeed, return the stake account (still
# delegated to Shinobi Systems) to the user, and take nothing from it.
destake_and_check destake_deactivated_after_redelegate 1 $ELSEWHERE_STAKE_B_PUBKEY $SHINOBI_SYSTEMS_VOTE_PUBKEY active


echo "All destake_deactivated_elsewhere tests passed"
