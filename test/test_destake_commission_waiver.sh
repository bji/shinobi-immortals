#!/bin/bash

# Tests of the Destake commission waiver: Destake does not charge commission once no commission charge has completed
# for the entry in DESTAKE_COMMISSION_WAIVER_EPOCHS epochs (see program/inc/constants.h), so that a user can always
# get their stake account back even if commission can no longer be charged.
#
# Usage: SOURCE=<root of the Shinobi Immortals source> test/test_destake_commission_waiver.sh
#
# This runs on its own test validator, separately from test/test.sh, because it needs the program built with a small
# DESTAKE_COMMISSION_WAIVER_EPOCHS (3), while the shared suite is built with a value far beyond any test's length so
# that its tests, which destake entries many epochs after staking them without running the commission crank, still
# see commission charged.
#
# The waiver is decided only from the epoch of the entry's last completed commission charge, never by attempting the
# charge, so it can be tested without making commission charges actually fail: the tests check that commission is
# charged when fewer than N epochs have passed since the last charge, and is not charged once N have, while commission
# is owed in both cases.
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

# Build the program so that Destake waives commission once 3 epochs have passed without a completed commission charge
export DESTAKE_COMMISSION_WAIVER_EPOCHS=3

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


# Byte offsets within Entry (program/inc/entry.h) of owned.last_commission_charge_stake_account_lamports, and of
# last_commission_charge_epoch, which is stored at the end of the Entry
ENTRY_COMMISSION_BASELINE_OFFSET=320
ENTRY_LAST_COMMISSION_CHARGE_EPOCH_OFFSET=3200


# Prints the entry account address of entry $1 of block 1 0, the block that these tests use
function test_entry ()
{
    local BLOCK_PUBKEY=`pda $SELF_PROGRAM_PUBKEY [ u8 14 u32 1 u32 0 ]`
    local MINT_PUBKEY=`pda $SELF_PROGRAM_PUBKEY [ u8 5 pubkey $BLOCK_PUBKEY u16 $1 ]`
    pda $SELF_PROGRAM_PUBKEY [ u8 15 pubkey $MINT_PUBKEY ]
}


# The commission value that means 100%: the denominator that compute_commission() divides by
COMMISSION_FULL_SCALE=65535

# Prints the commission in lamports that compute_commission() would charge on delegated stake $1 with baseline $2 at
# commission $3 (out of COMMISSION_FULL_SCALE)
function expected_commission ()
{
    local STAKE=$1
    local BASELINE=$2
    local COMMISSION=$3

    if [ $STAKE -le $BASELINE ]; then
        echo 0
    else
        echo $(((STAKE - BASELINE) * COMMISSION / COMMISSION_FULL_SCALE))
    fi
}


# Waits up to 60 seconds for the delegated stake of stake account $2 to rise far enough above entry $3's commission
# baseline that commission is owed, which happens when the epoch's staking rewards are paid (they are distributed over
# the first slots of an epoch, not all at once at its boundary); then waits two more seconds so that the cluster's
# epoch rewards period, during which the stake program refuses MoveStake, is over.  Fails test $1 if no commission
# becomes owed.
function wait_for_commission_owed ()
{
    local TEST_NAME=$1
    local STAKE_PUBKEY=$2
    local ENTRY_PUBKEY=$3

    local BASELINE=`account_u64 $ENTRY_PUBKEY $ENTRY_COMMISSION_BASELINE_OFFSET`
    local WAITED=0
    echo "Waiting for commission to be owed on $STAKE_PUBKEY"
    while [ `expected_commission \`stake_field $STAKE_PUBKEY delegatedStake\` $BASELINE $BLOCK_COMMISSION` -le 0 ]; do
        if [ $WAITED -ge 60 ]; then
            fail_test $TEST_NAME "no commission became owed on $STAKE_PUBKEY above the baseline $BASELINE"
        fi
        sleep 1
        WAITED=$((WAITED + 1))
    done
    sleep 2
}


# Destakes entry $2 with stake account $3, whose last commission charge was in epoch $5.  If $4 is "charged", checks
# that fewer than DESTAKE_COMMISSION_WAIVER_EPOCHS epochs had passed since then and that commission was charged; if $4
# is "waived", checks that at least that many had passed and that commission was not charged.  Epochs on the local
# cluster are short, so the number of epochs that have passed is measured both before and after destaking.  Fails
# test $1 if not.
function destake_and_check ()
{
    local TEST_NAME=$1
    local ENTRY_INDEX=$2
    local STAKE_PUBKEY=$3
    local EXPECTED=$4
    local LAST_CHARGE_EPOCH=$5
    local ENTRY_PUBKEY=`test_entry $ENTRY_INDEX`

    # The entry must record the expected last commission charge epoch
    if [ "`account_u64 $ENTRY_PUBKEY $ENTRY_LAST_COMMISSION_CHARGE_EPOCH_OFFSET`" != "$LAST_CHARGE_EPOCH" ]; then
        fail_test $TEST_NAME "the entry's last commission charge epoch is not $LAST_CHARGE_EPOCH"
    fi

    # Commission must be owed, or the test would pass whether or not it was waived
    local STAKE_BEFORE=`stake_field $STAKE_PUBKEY delegatedStake`
    local BASELINE=`account_u64 $ENTRY_PUBKEY $ENTRY_COMMISSION_BASELINE_OFFSET`
    local OWED=`expected_commission $STAKE_BEFORE $BASELINE $BLOCK_COMMISSION`
    if [ $OWED -le 0 ]; then
        fail_test $TEST_NAME "precondition: no commission is owed (stake $STAKE_BEFORE, baseline $BASELINE)"
    fi
    local EPOCHS_BEFORE=$((`current_epoch` - LAST_CHARGE_EPOCH))
    local MASTER_BEFORE=`stake_field $MASTER_STAKE_PUBKEY delegatedStake`

    assert $TEST_NAME                                                                                                \
    `SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/user_destake_tx.sh                                     \
         $RICH_USER1_PUBKEY 1 0 $ENTRY_INDEX $STAKE_PUBKEY                                                           \
        | solxact hash l                                                                                             \
        | solxact sign $LEDGER/rich_user1.json                                                                       \
        | solxact submit l 2>&1`

    local EPOCHS_AFTER=$((`current_epoch` - LAST_CHARGE_EPOCH))
    local STAKE_AFTER=`stake_field $STAKE_PUBKEY delegatedStake`
    local MASTER_AFTER=`stake_field $MASTER_STAKE_PUBKEY delegatedStake`

    if [ "$EXPECTED" = "charged" ]; then
        # The destake must have happened before the waiver applied
        if [ $EPOCHS_AFTER -ge $DESTAKE_COMMISSION_WAIVER_EPOCHS ]; then
            fail_test $TEST_NAME "$EPOCHS_AFTER epochs passed since the last charge before destaking; rerun the test"
        fi
        # The commission moved from the stake account into the master stake account
        if [ $((STAKE_BEFORE - STAKE_AFTER)) -ne $OWED ]; then
            fail_test $TEST_NAME "stake went from $STAKE_BEFORE to $STAKE_AFTER, expected a commission of $OWED"
        fi
        if [ $((MASTER_AFTER - MASTER_BEFORE)) -ne $OWED ]; then
            fail_test $TEST_NAME "master stake went from $MASTER_BEFORE to $MASTER_AFTER, expected an increase of $OWED"
        fi
    else
        # The destake must have happened once the waiver applied
        if [ $EPOCHS_BEFORE -lt $DESTAKE_COMMISSION_WAIVER_EPOCHS ]; then
            fail_test $TEST_NAME "only $EPOCHS_BEFORE epochs passed since the last charge before destaking"
        fi
        # Nothing moved
        if [ "$STAKE_AFTER" != "$STAKE_BEFORE" ]; then
            fail_test $TEST_NAME "stake went from $STAKE_BEFORE to $STAKE_AFTER; commission of $OWED was not waived"
        fi
        if [ "$MASTER_AFTER" != "$MASTER_BEFORE" ]; then
            fail_test $TEST_NAME "master stake went from $MASTER_BEFORE to $MASTER_AFTER; commission was not waived"
        fi
    fi

    # The stake account belongs to the user again
    if [ "`stake_field $STAKE_PUBKEY withdrawer`" != "$RICH_USER1_PUBKEY" ]; then
        fail_test $TEST_NAME "stake account was not returned to rich_user1"
    fi
    # The entry's record of its last commission charge epoch is cleared along with the rest of its staking records
    if [ "`account_u64 $ENTRY_PUBKEY $ENTRY_LAST_COMMISSION_CHARGE_EPOCH_OFFSET`" != "0" ]; then
        fail_test $TEST_NAME "the entry's last commission charge epoch was not cleared"
    fi
}


# ---------------------------------------------------------------------------------------------------------------------
# Setup
# ---------------------------------------------------------------------------------------------------------------------

# The block's commission: 6553 / 65535, about 10%
BLOCK_COMMISSION=6553

# Entry metadata: all zeroes except for the first byte, which differs between entries so that their hashes differ
BYTE_0=`echo -n 0 | base64 | tr -d '\n'`
BYTE_1=`echo -n 1 | base64 | tr -d '\n'`
METADATA0=`(echo -n 0; dd if=/dev/zero bs=1 count=2843 status=none) | base64 | tr -d '\n'`
METADATA1=`(echo -n 1; dd if=/dev/zero bs=1 count=2843 status=none) | base64 | tr -d '\n'`
SALT0=0
SALT1=1
SHA2560=`compute_metadata_sha256 $METADATA0 $SALT0`
SHA2561=`compute_metadata_sha256 $METADATA1 $SALT1`

# Initialize the program
assert commission_waiver_setup_initialize                                                                            \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY SHINOBI_SYSTEMS_VOTE_PUBKEY=$SHINOBI_SYSTEMS_VOTE_PUBKEY                   \
     $SOURCE/scripts/super_initialize_tx.sh $SUPERUSER_PUBKEY $ADMIN_PUBKEY                                          \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/super.json                                                                                \
    | solxact submit l 2>&1`

# Create block 1 0: complete and revealed, with two entries
assert commission_waiver_setup_create_block                                                                          \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/admin_create_block_tx.sh                                   \
     $ADMIN_PUBKEY 1 0 $BLOCK_COMMISSION 2 0 $((24*60*60)) \`lamports_from_sol 1000\` 1                              \
     \`lamports_from_sol 1\` false $((24*60*60)) \`lamports_from_sol 1000\` 0                                        \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/admin.json                                                                                \
    | solxact submit l 2>&1`
assert commission_waiver_setup_add_entries                                                                           \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/admin_add_entries_to_block_tx.sh                           \
     $ADMIN_PUBKEY 1 0 "http://foo.bar.com" none 0 $SHA2560 $SHA2561                                                 \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/admin.json                                                                                \
    | solxact submit l 2>&1`
assert commission_waiver_setup_set_metadata_0                                                                        \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/admin_set_metadata_bytes_tx.sh                             \
     $ADMIN_PUBKEY 1 0 0 0 $BYTE_0                                                                                   \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/admin.json                                                                                \
    | solxact submit l 2>&1`
assert commission_waiver_setup_set_metadata_1                                                                        \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/admin_set_metadata_bytes_tx.sh                             \
     $ADMIN_PUBKEY 1 0 1 0 $BYTE_1                                                                                   \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/admin.json                                                                                \
    | solxact submit l 2>&1`
assert commission_waiver_setup_reveal                                                                                \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/admin_reveal_entries_tx.sh                                 \
     $ADMIN_PUBKEY 1 0 0 $SALT0 $SALT1                                                                               \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/admin.json                                                                                \
    | solxact submit l 2>&1`
# rich_user1 buys both entries
for i in 0 1; do
    assert commission_waiver_setup_buy_$i                                                                            \
    `SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/user_buy_tx.sh                                         \
         $ADMIN_PUBKEY $RICH_USER1_PUBKEY 1 0 $i \`lamports_from_sol 1001\`                                          \
        | solxact hash l                                                                                             \
        | solxact sign $LEDGER/rich_user1.json                                                                       \
        | solxact submit l 2>&1`
done

# Create two undelegated stake accounts owned by rich_user1, and wait for the next epoch so that the master stake
# account is fully active
for i in a b; do
    make_stake_account $LEDGER/rich_user1.json $LEDGER/stake_$i.json 1000
done
STAKE_A_PUBKEY=`solxact pubkey $LEDGER/stake_a.json`
STAKE_B_PUBKEY=`solxact pubkey $LEDGER/stake_b.json`
sleep_until_next_epoch

# Stake entry 0 with stake_a and entry 1 with stake_b, in the same epoch.  Stake delegates them to Shinobi Systems,
# and records the current epoch as each entry's last commission charge epoch.
STAKE_EPOCH=`current_epoch`
assert commission_waiver_setup_stake_0                                                                               \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/user_stake_tx.sh                                           \
     $RICH_USER1_PUBKEY 1 0 0 $STAKE_A_PUBKEY                                                                        \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/rich_user1.json                                                                           \
    | solxact submit l 2>&1`
assert commission_waiver_setup_stake_1                                                                               \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/user_stake_tx.sh                                           \
     $RICH_USER1_PUBKEY 1 0 1 $STAKE_B_PUBKEY                                                                        \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/rich_user1.json                                                                           \
    | solxact submit l 2>&1`
if [ "`current_epoch`" != "$STAKE_EPOCH" ]; then
    fail_test commission_waiver_setup_stake "the epoch changed while staking; rerun the test"
fi


# ---------------------------------------------------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------------------------------------------------

# Stake records the epoch in which the entry was staked as its last commission charge epoch
TEST_NAME=commission_waiver_stake_records_epoch
for i in 0 1; do
    if [ "`account_u64 \`test_entry $i\` $ENTRY_LAST_COMMISSION_CHARGE_EPOCH_OFFSET`" != "$STAKE_EPOCH" ]; then
        fail_test $TEST_NAME "entry $i's last commission charge epoch is not the staking epoch $STAKE_EPOCH"
    fi
done


# The stake accounts become fully active in the next epoch, and are first paid rewards at the start of the one after.
# Once commission is owed on entry 0, run the commission crank on entry 0 only.  The charge completes and records the
# current epoch.
sleep_until_next_epoch
sleep_until_next_epoch
TEST_NAME=commission_waiver_crank_records_epoch
wait_for_commission_owed ${TEST_NAME}_rewards $STAKE_A_PUBKEY `test_entry 0`
CRANK_EPOCH=`current_epoch`
STAKE_BEFORE=`stake_field $STAKE_A_PUBKEY delegatedStake`
assert $TEST_NAME                                                                                                    \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/anyone_take_commission_or_delegate_tx.sh                   \
     $RICH_USER2_PUBKEY 1 0 0 $STAKE_A_PUBKEY                                                                        \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/rich_user2.json                                                                           \
    | solxact submit l 2>&1`
if [ "`current_epoch`" != "$CRANK_EPOCH" ]; then
    fail_test $TEST_NAME "the epoch changed while running the crank; rerun the test"
fi
if [ "`stake_field $STAKE_A_PUBKEY delegatedStake`" = "$STAKE_BEFORE" ]; then
    fail_test $TEST_NAME "the crank charged no commission"
fi
if [ "`account_u64 \`test_entry 0\` $ENTRY_LAST_COMMISSION_CHARGE_EPOCH_OFFSET`" != "$CRANK_EPOCH" ]; then
    fail_test $TEST_NAME "entry 0's last commission charge epoch is not the crank's epoch $CRANK_EPOCH"
fi
# Entry 1 was not cranked, and still records the staking epoch
if [ "`account_u64 \`test_entry 1\` $ENTRY_LAST_COMMISSION_CHARGE_EPOCH_OFFSET`" != "$STAKE_EPOCH" ]; then
    fail_test $TEST_NAME "entry 1's last commission charge epoch changed"
fi


# In the next epoch, more rewards are paid, so commission is owed on entry 0 again.  Its last charge was 1 epoch ago,
# fewer than the waiver's 3, so Destake charges commission.
sleep_until_next_epoch
wait_for_commission_owed commission_waiver_not_yet_rewards $STAKE_A_PUBKEY `test_entry 0`
destake_and_check commission_waiver_not_yet 0 $STAKE_A_PUBKEY charged $CRANK_EPOCH


# Entry 1 has had no commission charge since it was staked.  Once 3 epochs have passed since then, Destake does not
# charge commission, although commission is owed.
while [ $((`current_epoch` - STAKE_EPOCH)) -lt $DESTAKE_COMMISSION_WAIVER_EPOCHS ]; do
    sleep_until_next_epoch
done
wait_for_commission_owed commission_waiver_waived_rewards $STAKE_B_PUBKEY `test_entry 1`
destake_and_check commission_waiver_waived 1 $STAKE_B_PUBKEY waived $STAKE_EPOCH


echo "All destake commission waiver tests passed"
