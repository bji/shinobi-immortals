#!/bin/bash

set -e

# Emits an encoded transaction that thaws the token account holding an entry's token.  This is only needed after a
# destake that left the token account frozen (user_destake_tx.sh with no_thaw).  It must be signed by the owner of the
# token account, and only succeeds if the entry is not staked and the token account is frozen.  The entry cannot be
# staked again until its token account has been thawed.

function require ()
{
    if [ -z "$1" ]; then
        cat <<EOF

Usage: user_thaw_tx.sh <USER_PUBKEY> <GROUP_NUMBER> <BLOCK_NUMBER> <ENTRY_INDEX> [<TOKEN_PUBKEY>]

If <TOKEN_PUBKEY> is not provided, it is assumed to be the Associated Token Account.

EOF
        exit 1
    fi
}

USER_PUBKEY=$1
GROUP_NUMBER=$2
BLOCK_NUMBER=$3
ENTRY_INDEX=$4
TOKEN_PUBKEY=$5

require $USER_PUBKEY
require $GROUP_NUMBER
require $BLOCK_NUMBER
require $ENTRY_INDEX

# Compute program, block, entry, and related pubkeys

if [ -z "$SELF_PROGRAM_PUBKEY" ]; then
        SELF_PROGRAM_PUBKEY="Shin1cdrR1pmemXZU3yDV3PnQ48SX9UmrtHF4GbKzWG"
fi
   SPL_TOKEN_PROGRAM_PUBKEY="TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA"
      SPLATA_PROGRAM_PUBKEY="ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL"
           AUTHORITY_PUBKEY="pda $SELF_PROGRAM_PUBKEY [ u8 2 ]"
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

# The token account defaults to the user's Associated Token Account for the entry's mint
if [ -z "$TOKEN_PUBKEY" ]; then
               TOKEN_PUBKEY="pda $SPLATA_PROGRAM_PUBKEY                                                               \
                                 [ pubkey $USER_PUBKEY                                                                \
                                   pubkey $SPL_TOKEN_PROGRAM_PUBKEY                                                   \
                                   $ENTRY_MINT_PUBKEY ]"
fi

solxact encode                                                                                                        \
        encoding c                                                                                                    \
        fee_payer $USER_PUBKEY                                                                                        \
        program $SELF_PROGRAM_PUBKEY                                                                                  \
        account $BLOCK_PUBKEY                                                                                         \
        account $ENTRY_PUBKEY                                                                                         \
        // The token owner must sign //                                                                               \
        account $USER_PUBKEY s                                                                                        \
        // The token account is writable because it is thawed //                                                      \
        account $TOKEN_PUBKEY w                                                                                       \
        // The entry mint is needed to thaw the token account //                                                      \
        account $ENTRY_MINT_PUBKEY                                                                                    \
        // The program authority, which is the freeze authority of the entry mint //                                  \
        account $AUTHORITY_PUBKEY                                                                                     \
        account $SPL_TOKEN_PROGRAM_PUBKEY                                                                             \
        // Instruction code 15 = Thaw //                                                                              \
        u8 15
