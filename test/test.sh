#!/bin/sh

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

source $SOURCE/test/common

setup

source $SOURCE/test/test_super_initialize

source $SOURCE/test/test_super_set_admin

source $SOURCE/test/test_admin_create_block

source $SOURCE/test/test_admin_add_entries_to_block

source $SOURCE/test/test_admin_set_metadata_bytes

source $SOURCE/test/test_admin_reveal_entries

source $SOURCE/test/test_admin_set_block_commission

source $SOURCE/test/test_admin_split_master_stake

# teardown
