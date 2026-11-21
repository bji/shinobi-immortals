#!/bin/bash

set -e

# Emits an encoded transaction that initializes a cluster for use with Shinobi Immortals.  The superuser pays for all
# of the accounts that are created, including the reference stake account, which is funded with 2 SOL (see
# REFERENCE_STAKE_ACCOUNT_LAMPORTS in program/inc/constants.h) plus its rent exempt reserve, and which is delegated to
# Shinobi Systems.

function require ()
{
    if [ -z "$1" ]; then
        cat <<EOF

Usage: super_initialize_tx.sh <SUPERUSER_PUBKEY> <ADMIN_PUBKEY> <INITIAL_REWARD_BPS>

INITIAL_REWARD_BPS is the annual reward rate, in basis points, that a delegator to Shinobi Systems is estimated to
earn (650 is 6.50% per year), from 100 to 2000.  LevelUp uses it to calibrate the level credit curve until
MeasureRewardRate measures the real rate, which moves the rate from it gradually.

EOF
        exit 1
    fi
}

SUPERUSER_PUBKEY=$1
ADMIN_PUBKEY=$2
INITIAL_REWARD_BPS=$3

require $SUPERUSER_PUBKEY
require $ADMIN_PUBKEY
require $INITIAL_REWARD_BPS

# Compute program, block, entry, and related pubkeys.  These may not all be valid, depending on input parameters
# to the script, but any that are invalid won't be used by the solxact encode command that follows.

if [ -z "$SELF_PROGRAM_PUBKEY" ]; then
        SELF_PROGRAM_PUBKEY="Shin1cdrR1pmemXZU3yDV3PnQ48SX9UmrtHF4GbKzWG"
fi
if [ -z $SHINOBI_SYSTEMS_VOTE_PUBKEY ]; then
SHINOBI_SYSTEMS_VOTE_PUBKEY="BLADE1qNA1uNjRgER6DtUFf7FU3c1TWLLdpPeEcKatZ2"
fi
      SYSTEM_PROGRAM_PUBKEY="11111111111111111111111111111111"
   SPL_TOKEN_PROGRAM_PUBKEY="TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA"
      SPLATA_PROGRAM_PUBKEY="ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL"
       STAKE_PROGRAM_PUBKEY="Stake11111111111111111111111111111111111111"
        CLOCK_SYSVAR_PUBKEY="SysvarC1ock11111111111111111111111111111111"
         RENT_SYSVAR_PUBKEY="SysvarRent111111111111111111111111111111111"
    METAPLEX_PROGRAM_PUBKEY="metaqbxxUerdq28cj1RbAWkYQm3ybzjb6a8bt518x1s"
STAKE_HISTORY_SYSVAR_PUBKEY="SysvarStakeHistory1111111111111111111111111"
        STAKE_CONFIG_PUBKEY="StakeConfig11111111111111111111111111111111"
     REFERENCE_STAKE_PUBKEY="pda $SELF_PROGRAM_PUBKEY [ u8 12 ]"
              STATE_PUBKEY="pda $SELF_PROGRAM_PUBKEY [ u8 1 ]"
           AUTHORITY_PUBKEY="pda $SELF_PROGRAM_PUBKEY [ u8 2 ]"
             KI_MINT_PUBKEY="pda $SELF_PROGRAM_PUBKEY [ u8 3 ]"
     BID_MARKER_MINT_PUBKEY="pda $SELF_PROGRAM_PUBKEY [ u8 7 ]"
         KI_METADATA_PUBKEY="pda $METAPLEX_PROGRAM_PUBKEY                                                             \
                                 [ string metadata                                                                    \
                                   pubkey $METAPLEX_PROGRAM_PUBKEY                                                    \
                                   $KI_MINT_PUBKEY ]"
 BID_MARKER_METADATA_PUBKEY="pda $METAPLEX_PROGRAM_PUBKEY                                                             \
                                 [ string metadata                                                                    \
                                   pubkey $METAPLEX_PROGRAM_PUBKEY                                                    \
                                   $BID_MARKER_MINT_PUBKEY ]"
               BLOCK_PUBKEY="pda $SELF_PROGRAM_PUBKEY                                                                 \
                                 [ u8 10                                                                              \
                                   u32 $GROUP_NUMBER                                                                  \
                                   u32 $BLOCK_NUMBER ]"
           WHITELIST_PUBKEY="pda $SELF_PROGRAM_PUBKEY                                                                 \
                                 [ u8 9                                                                               \
                                   $BLOCK_PUBKEY ]"
          ENTRY_MINT_PUBKEY="pda $SELF_PROGRAM_PUBKEY                                                                 \
                                 [ u8 4                                                                               \
                                   $BLOCK_PUBKEY                                                                      \
                                   u16 $ENTRY_INDEX ]"
               ENTRY_PUBKEY="pda $SELF_PROGRAM_PUBKEY                                                                 \
                                 [ u8 11                                                                              \
                                   $ENTRY_MINT_PUBKEY ]"
         ENTRY_TOKEN_PUBKEY="pda $SELF_PROGRAM_PUBKEY                                                                 \
                                 [ u8 5                                                                               \
                                   $ENTRY_MINT_PUBKEY ]"
      ENTRY_METADATA_PUBKEY="pda $METAPLEX_PROGRAM_PUBKEY                                                             \
                                 [ string metadata                                                                    \
                                   pubkey $METAPLEX_PROGRAM_PUBKEY                                                    \
                                   $ENTRY_MINT_PUBKEY ]"
   TOKEN_DESTINATION_PUBKEY="pda $SPLATA_PROGRAM_PUBKEY                                                               \
                                 [ pubkey $USER_PUBKEY                                                                \
                                   pubkey $SPL_TOKEN_PROGRAM_PUBKEY                                                   \
                                   $ENTRY_MINT_PUBKEY ]"
    BID_MARKER_TOKEN_PUBKEY="pda $SELF_PROGRAM_PUBKEY                                                                 \
                                 [ u8 8                                                                               \
                                   $ENTRY_MINT_PUBKEY                                                                 \
                                   pubkey $USER_PUBKEY ]"
                 BID_PUBKEY="pda $SELF_PROGRAM_PUBKEY                                                                 \
                                 [ u8 6                                                                               \
                                   $BID_MARKER_TOKEN_PUBKEY ]"

solxact encode                                                                                                        \
        encoding c                                                                                                    \
        fee_payer $SUPERUSER_PUBKEY                                                                                   \
        program $SELF_PROGRAM_PUBKEY                                                                                  \
        account $SUPERUSER_PUBKEY ws                                                                                  \
        account $STATE_PUBKEY w                                                                                      \
        account $AUTHORITY_PUBKEY w                                                                                   \
        account $KI_MINT_PUBKEY w                                                                                     \
        account $KI_METADATA_PUBKEY w                                                                                 \
        account $BID_MARKER_MINT_PUBKEY w                                                                             \
        account $BID_MARKER_METADATA_PUBKEY w                                                                         \
        account $RENT_SYSVAR_PUBKEY                                                                                   \
        account $SYSTEM_PROGRAM_PUBKEY                                                                                \
        account $SPL_TOKEN_PROGRAM_PUBKEY                                                                             \
        account $METAPLEX_PROGRAM_PUBKEY                                                                              \
        account $REFERENCE_STAKE_PUBKEY w                                                                             \
        account $SHINOBI_SYSTEMS_VOTE_PUBKEY                                                                          \
        account $CLOCK_SYSVAR_PUBKEY                                                                                  \
        account $STAKE_HISTORY_SYSVAR_PUBKEY                                                                          \
        account $STAKE_CONFIG_PUBKEY                                                                                  \
        account $STAKE_PROGRAM_PUBKEY                                                                                 \
        // Instruction code 0 = InitializeData //                                                                     \
        u8 0                                                                                                          \
        // Admin pubkey //                                                                                            \
        pubkey $ADMIN_PUBKEY                                                                                          \
        // Initial reward rate, in basis points //                                                                    \
        u32 $INITIAL_REWARD_BPS
