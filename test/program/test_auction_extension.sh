#!/bin/bash

# Tests of auction bidding rules: every bid after the first must exceed the current highest bid by at least
# MINIMUM_BID_INCREMENT_BPS (5%), and a bid placed with fewer than AUCTION_EXTENSION_SECONDS left in an auction
# extends the auction to end AUCTION_EXTENSION_SECONDS after the bid, as many times as bids keep arriving.
#
# Usage: SOURCE=<root of the Shinobi Immortals source> test/program/test_auction_extension.sh
#
# This runs on its own test validator, separately from test/program/test.sh, because it needs the program built with
# auction extensions turned on.  The shared suite turns them off (see AUCTION_EXTENSION_SECONDS in test/program/common),
# because its auctions last only 20 seconds and its tests wait a fixed time for them to end.  Here the extension window
# is 20 seconds rather than the production 10 minutes, so that the tests do not take long.
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

# Build the program so that a bid with fewer than 20 seconds left in an auction extends it
export AUCTION_EXTENSION_SECONDS=20

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


# The length of the auction, in seconds, that the block is created with
AUCTION_DURATION=60


# The entry that these tests use: entry 0 of block 1 0
BLOCK_PUBKEY=`pda $SELF_PROGRAM_PUBKEY [ u8 10 u32 1 u32 0 ]`
MINT_PUBKEY=`pda $SELF_PROGRAM_PUBKEY [ u8 4 pubkey $BLOCK_PUBKEY u16 0 ]`
ENTRY_PUBKEY=`pda $SELF_PROGRAM_PUBKEY [ u8 11 pubkey $MINT_PUBKEY ]`


# Prints the entry's reveal_timestamp, the signed 64 bit value at byte offset 200 of the Entry
function entry_reveal_timestamp ()
{
    get_account_data $ENTRY_PUBKEY 200 8 | base64 -d | od -An -td8 -v | tr -d '[:space:]'
}


# Prints the entry's duration, the unsigned 32 bit value at byte offset 156 of the Entry
function entry_duration ()
{
    get_account_data $ENTRY_PUBKEY 156 4 | base64 -d | od -An -tu4 -v | tr -d '[:space:]'
}


# Prints the entry's auction end: reveal_timestamp + duration
function entry_auction_end ()
{
    echo $((`entry_reveal_timestamp` + `entry_duration`))
}


# Prints the entry's highest bid in lamports, the unsigned 64 bit value at byte offset 224 of the Entry
function entry_highest_bid ()
{
    get_account_data $ENTRY_PUBKEY 224 8 | base64 -d | od -An -tu8 -v | tr -d '[:space:]'
}


# Prints the smallest bid allowed after a highest bid of $1 lamports: $1 plus 5%, rounded up
function next_minimum_bid ()
{
    echo $(($1 + (($1 * 500 + 9999) / 10000)))
}


# Waits until the cluster's clock reaches unix timestamp $1
function wait_for_cluster_time ()
{
    while [ `clock_unix_timestamp` -lt $1 ]; do
        sleep 1
    done
}


# Submits a bid of exactly $3 lamports on the entry by the user whose key file is $1 and whose pubkey is $2, and
# prints the result of submission
function bid ()
{
    SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/user_bid_tx.sh $2 1 0 0 $3 $3                         \
        | solxact hash l                                                                                             \
        | solxact sign $1                                                                                            \
        | solxact submit l 2>&1
}


# Places a bid of exactly $4 lamports by key file $2 and pubkey $3, which must succeed, as test $1.  Then checks that
# the auction's end was moved to AUCTION_EXTENSION_SECONDS after the bid if the bid was placed with fewer than that
# many seconds left, and left alone otherwise.  The cluster's clock is read just before and just after the bid, and the
# bid's own time lies between them.
function bid_and_check_end ()
{
    local TEST_NAME=$1
    local END_BEFORE=`entry_auction_end`
    local TIME_BEFORE=`clock_unix_timestamp`

    assert $TEST_NAME `bid $2 $3 $4`

    local TIME_AFTER=`clock_unix_timestamp`
    local END_AFTER=`entry_auction_end`

    if [ "`entry_highest_bid`" != "$4" ]; then
        fail_test $TEST_NAME "highest bid is `entry_highest_bid`, expected $4"
    fi

    # The end is never moved earlier
    if [ $END_AFTER -lt $END_BEFORE ]; then
        fail_test $TEST_NAME "the auction end moved earlier, from $END_BEFORE to $END_AFTER"
    fi
    if [ $((TIME_AFTER + AUCTION_EXTENSION_SECONDS)) -le $END_BEFORE ]; then
        # The bid had more than the extension window left, so the end must not have moved
        if [ $END_AFTER -ne $END_BEFORE ]; then
            fail_test $TEST_NAME "an early bid moved the auction end from $END_BEFORE to $END_AFTER"
        fi
    elif [ $((TIME_BEFORE + AUCTION_EXTENSION_SECONDS)) -gt $END_BEFORE ]; then
        # The bid had less than the extension window left, so the end must now be the window after the bid
        if [ $END_AFTER -lt $((TIME_BEFORE + AUCTION_EXTENSION_SECONDS)) ] ||
           [ $END_AFTER -gt $((TIME_AFTER + AUCTION_EXTENSION_SECONDS)) ]; then
            fail_test $TEST_NAME "auction end is $END_AFTER, expected $AUCTION_EXTENSION_SECONDS seconds after the bid"\
                                 "(placed between $TIME_BEFORE and $TIME_AFTER)"
        fi
    else
        fail_test $TEST_NAME "the bid was placed too close to the edge of the extension window to check; rerun"
    fi
}


# ---------------------------------------------------------------------------------------------------------------------
# Setup
# ---------------------------------------------------------------------------------------------------------------------

BYTE_0=`echo -n 0 | base64 | tr -d '\n'`
METADATA0=`(echo -n 0; dd if=/dev/zero bs=1 count=2843 status=none) | base64 | tr -d '\n'`
SALT0=0
SHA2560=`compute_metadata_sha256 $METADATA0 $SALT0`

# Initialize the program
assert auction_extension_setup_initialize                                                                            \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY SHINOBI_SYSTEMS_VOTE_PUBKEY=$SHINOBI_SYSTEMS_VOTE_PUBKEY                   \
     $SOURCE/scripts/super_initialize_tx.sh $SUPERUSER_PUBKEY $ADMIN_PUBKEY 490                                      \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/super.json                                                                                \
    | solxact submit l 2>&1`

# Create block 1 0: one entry, no mysteries, sold by an auction of AUCTION_DURATION seconds with a minimum price of
# 1 SOL
assert auction_extension_setup_create_block                                                                          \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/admin_create_block_tx.sh                                   \
     $ADMIN_PUBKEY 1 0 1 0 $((24*60*60)) \`lamports_from_sol 1000\` 1                                                \
     \`lamports_from_sol 1\` $AUCTION_DURATION 0 \`lamports_from_sol 1000\` 0                                        \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/admin.json                                                                                \
    | solxact submit l 2>&1`
assert auction_extension_setup_add_entries                                                                           \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/admin_add_entries_to_block_tx.sh                           \
     $ADMIN_PUBKEY 1 0 "http://foo.bar.com" none 0 $SHA2560                                                          \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/admin.json                                                                                \
    | solxact submit l 2>&1`
assert auction_extension_setup_set_metadata                                                                          \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/admin_set_metadata_bytes_tx.sh                             \
     $ADMIN_PUBKEY 1 0 0 0 $BYTE_0                                                                                   \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/admin.json                                                                                \
    | solxact submit l 2>&1`

# Reveal the entry, which starts its auction
assert auction_extension_setup_reveal                                                                                \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/admin_reveal_entries_tx.sh                                 \
     $ADMIN_PUBKEY 1 0 0 $SALT0                                                                                      \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/admin.json                                                                                \
    | solxact submit l 2>&1`
ORIGINAL_END=`entry_auction_end`
if [ "`entry_duration`" != "$AUCTION_DURATION" ]; then
    fail_test auction_extension_setup_reveal "entry duration is `entry_duration`, expected $AUCTION_DURATION"
fi


# ---------------------------------------------------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------------------------------------------------

# The first bid must be at least the minimum price; less is refused (0x3fe = 1022 = Error_BidTooLow)
assert_fail auction_extension_first_bid_too_low                                                                      \
'Transaction simulation failed: Error processing Instruction 0: custom program error: 0x3fe'                         \
`bid $LEDGER/rich_user1.json $RICH_USER1_PUBKEY $((\`lamports_from_sol 1\` - 1))`


# A first bid of exactly the minimum price, placed early in the auction, is accepted and does not move its end
FIRST_BID=`lamports_from_sol 1`
if [ $((`clock_unix_timestamp` + AUCTION_EXTENSION_SECONDS + 5)) -gt $ORIGINAL_END ]; then
    fail_test auction_extension_early_bid "setup took too long; the auction is already near its end; rerun"
fi
bid_and_check_end auction_extension_early_bid $LEDGER/rich_user1.json $RICH_USER1_PUBKEY $FIRST_BID
if [ "`entry_auction_end`" != "$ORIGINAL_END" ]; then
    fail_test auction_extension_early_bid "the auction end moved from $ORIGINAL_END to `entry_auction_end`"
fi


# A bid less than 5% above the highest bid is refused, even by 1 lamport
MINIMUM=`next_minimum_bid $FIRST_BID`
assert_fail auction_extension_increment_too_small                                                                    \
'Transaction simulation failed: Error processing Instruction 0: custom program error: 0x3fe'                         \
`bid $LEDGER/rich_user2.json $RICH_USER2_PUBKEY $((MINIMUM - 1))`


# Wait until fewer than AUCTION_EXTENSION_SECONDS remain (6 seconds, so that the extended end is well after the
# original end), then outbid by exactly 5%: the bid is accepted and moves the auction's end to
# AUCTION_EXTENSION_SECONDS after it
wait_for_cluster_time $((ORIGINAL_END - 6))
bid_and_check_end auction_extension_late_bid $LEDGER/rich_user2.json $RICH_USER2_PUBKEY $MINIMUM
FIRST_EXTENDED_END=`entry_auction_end`
if [ $FIRST_EXTENDED_END -le $ORIGINAL_END ]; then
    fail_test auction_extension_late_bid "the auction end was not extended past $ORIGINAL_END"
fi


# Once the original end has passed, the auction is still open because it was extended, and another late bid extends
# it again
wait_for_cluster_time $((ORIGINAL_END + 2))
if [ $((`clock_unix_timestamp` + 3)) -ge $FIRST_EXTENDED_END ]; then
    fail_test auction_extension_second_extension "the extended auction is already nearly over; rerun"
fi
SECOND_BID=`next_minimum_bid $MINIMUM`
bid_and_check_end auction_extension_second_extension $LEDGER/rich_user1.json $RICH_USER1_PUBKEY $SECOND_BID
SECOND_EXTENDED_END=`entry_auction_end`
if [ $SECOND_EXTENDED_END -le $FIRST_EXTENDED_END ]; then
    fail_test auction_extension_second_extension "the auction end was not extended past $FIRST_EXTENDED_END"
fi


# Once the extended end has passed, no more bids are accepted (0x3fd = 1021 = Error_EntryNotInAuction)
wait_for_cluster_time $((SECOND_EXTENDED_END + 2))
assert_fail auction_extension_bid_after_end                                                                          \
'Transaction simulation failed: Error processing Instruction 0: custom program error: 0x3fd'                         \
`bid $LEDGER/rich_user2.json $RICH_USER2_PUBKEY \`next_minimum_bid $SECOND_BID\``


# The winner settles the auction after its extended end, and receives the entry's token
assert auction_extension_settle                                                                                      \
`SELF_PROGRAM_PUBKEY=$SELF_PROGRAM_PUBKEY $SOURCE/scripts/anyone_settle_winning_bid_tx.sh                            \
     $RICH_USER1_PUBKEY $ADMIN_PUBKEY $RICH_USER1_PUBKEY 1 0 0                                                       \
    | solxact hash l                                                                                                 \
    | solxact sign $LEDGER/rich_user1.json                                                                           \
    | solxact submit l 2>&1`
if [ "`get_token_balance $MINT_PUBKEY $RICH_USER1_PUBKEY`" != "1" ]; then
    fail_test auction_extension_settle "the winning bidder did not receive the entry's token"
fi


echo "All auction extension tests passed"
