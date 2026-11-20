#!/bin/sh

set -e

# Usage: test.sh [<TEST_TO_RUN>...]
# If no tests are specified, all tests are run.

if [ -z "$SOURCE" ]; then
    echo "The SOURCE variable must be set to the root directory of the Shinobi Immortals source"
    exit 1
fi

TESTS=
while [ -n "$1" ]; do
    TESTS="$TESTS [$1]"
    shift
done

source $SOURCE/test/program/common

setup

source $SOURCE/test/program/test_super_initialize

source $SOURCE/test/program/test_super_set_admin

source $SOURCE/test/program/test_admin_create_block

source $SOURCE/test/program/test_admin_add_entries_to_block

source $SOURCE/test/program/test_admin_set_metadata_bytes

source $SOURCE/test/program/test_admin_reveal_entries

source $SOURCE/test/program/test_admin_add_whitelist_entries

source $SOURCE/test/program/test_admin_delete_whitelist

source $SOURCE/test/program/test_admin_withdraw_excess

source $SOURCE/test/program/test_user_buy

source $SOURCE/test/program/test_user_refund

source $SOURCE/test/program/test_user_bid

source $SOURCE/test/program/test_user_claim_losing

source $SOURCE/test/program/test_user_stake

source $SOURCE/test/program/test_user_destake

source $SOURCE/test/program/test_user_thaw

source $SOURCE/test/program/test_user_harvest

source $SOURCE/test/program/test_user_level_up

source $SOURCE/test/program/test_anyone_settle_winning_bid

source $SOURCE/test/program/test_anyone_delegate

source $SOURCE/test/program/test_anyone_update_epochs_staked

source $SOURCE/test/program/test_special_reauthorize
