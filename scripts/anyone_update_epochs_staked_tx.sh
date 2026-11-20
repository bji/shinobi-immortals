#!/bin/bash

set -e

# Emits an encoded transaction that brings an entry's epochs_staked up to date, so that it is exact as of the current
# epoch.  Anyone may do this.  If the entry is not staked, this does nothing (its epochs_staked is already exact), and
# the stake account need not be given.

function require ()
{
    if [ -z "$1" ]; then
        cat <<EOF

Usage: anyone_update_epochs_staked_tx.sh <FEE_PAYER_PUBKEY> <GROUP_NUMBER> <BLOCK_NUMBER> <ENTRY_INDEX> \\
                                         [<ENTRY_STAKE_ACCOUNT_PUBKEY>|none]

If the entry is staked, <ENTRY_STAKE_ACCOUNT_PUBKEY> must be its stake account.  If the entry is not staked, it may
be omitted or given as "none".

EOF
        exit 1
    fi
}

FEE_PAYER_PUBKEY=$1
GROUP_NUMBER=$2
BLOCK_NUMBER=$3
ENTRY_INDEX=$4
ENTRY_STAKE_ACCOUNT_PUBKEY=$5

require $FEE_PAYER_PUBKEY
require $GROUP_NUMBER
require $BLOCK_NUMBER
require $ENTRY_INDEX

# Compute program, block, and entry pubkeys

if [ -z "$SELF_PROGRAM_PUBKEY" ]; then
        SELF_PROGRAM_PUBKEY="Shin1cdrR1pmemXZU3yDV3PnQ48SX9UmrtHF4GbKzWG"
fi
      SYSTEM_PROGRAM_PUBKEY="11111111111111111111111111111111"
               BLOCK_PUBKEY="pda $SELF_PROGRAM_PUBKEY                                                                 \
                                 [ u8 10                                                                              \
                                   u32 $GROUP_NUMBER                                                                  \
                                   u32 $BLOCK_NUMBER ]"
          ENTRY_MINT_PUBKEY="pda $SELF_PROGRAM_PUBKEY                                                                 \
                                 [ u8 4                                                                               \
                                   $BLOCK_PUBKEY                                                                      \
                                   u16 $ENTRY_INDEX ]"
               ENTRY_PUBKEY="pda $SELF_PROGRAM_PUBKEY                                                                 \
                                 [ u8 11                                                                              \
                                   $ENTRY_MINT_PUBKEY ]"

# The stake account is not used if the entry is not staked; the system program serves as a placeholder
if [ -z "$ENTRY_STAKE_ACCOUNT_PUBKEY" ] || [ "$ENTRY_STAKE_ACCOUNT_PUBKEY" = "none" ]; then
       ENTRY_STAKE_ACCOUNT_PUBKEY=$SYSTEM_PROGRAM_PUBKEY
fi

solxact encode                                                                                                        \
        encoding c                                                                                                    \
        fee_payer $FEE_PAYER_PUBKEY                                                                                   \
        program $SELF_PROGRAM_PUBKEY                                                                                  \
        account $ENTRY_PUBKEY w                                                                                       \
        account $ENTRY_STAKE_ACCOUNT_PUBKEY                                                                           \
        // Instruction code 20 = UpdateEpochsStaked //                                                                \
        u8 20
