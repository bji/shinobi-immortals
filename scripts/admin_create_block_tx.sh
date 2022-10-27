#!/bin/sh

set -e

# Emits an encoded transaction that creates a block.  Assumes that admin is the funding_account.

function require ()
{
    if [ -z "$1" ]; then
        cat <<EOF

Usage: admin_create_block_tx.sh <ADMIN_PUBKEY> <GROUP_NUMBER> <BLOCK_NUMBER> <COMMISSION (0-65535)> \\
                                <TOTAL_ENTRY_COUNT> <TOTAL_MYSTERY_COUNT> <MYSTERY_PHASE_DURATION> \\
                                <MYSTERY_START_PRICE_LAMPORTS> <REVEAL_PERIOD_DURATION> <MINIMUM_PRICE_LAMPORTS> \\
                                <HAS_AUCTION> <DURATION> <FINAL_START_PRICE_LAMPORTS> <WHITELIST_DURATION>

EOF
        exit 1
    fi
}

function pda ()
{
    solpda $@ | cut -d . -f 1
}

ADMIN_PUBKEY=$1
GROUP_NUMBER=$2
BLOCK_NUMBER=$3
COMMISSION=$4
TOTAL_ENTRY_COUNT=$5
TOTAL_MYSTERY_COUNT=$6
MYSTERY_PHASE_DURATION=$7
MYSTERY_START_PRICE_LAMPORTS=$8
REVEAL_PERIOD_DURATION=$9
MINIMUM_PRICE_LAMPORTS=${10}
HAS_AUCTION=${11}
DURATION=${12}
FINAL_START_PRICE_LAMPORTS=${13}
WHITELIST_DURATION=${14}

require $ADMIN_PUBKEY
require $GROUP_NUMBER
require $BLOCK_NUMBER
require $COMMISSION
require $TOTAL_ENTRY_COUNT
require $TOTAL_MYSTERY_COUNT
require $MYSTERY_PHASE_DURATION
require $MYSTERY_START_PRICE_LAMPORTS
require $REVEAL_PERIOD_DURATION
require $MINIMUM_PRICE_LAMPORTS
require $HAS_AUCTION
require $DURATION
require $FINAL_START_PRICE_LAMPORTS
require $WHITELIST_DURATION

if [ "$HAS_AUCTION" = "1" ]; then
    HAS_AUCTION=true
elif [ "$HAS_AUCTION" = "0" ]; then
    HAS_AUCTION=false
fi

if [ -e "$ADMIN_PUBKEY" ]; then
    ADMIN_PUBKEY=$(solpda -pubkey "$ADMIN_PUBKEY")
fi

# Compute pubkeys

      SYSTEM_PROGRAM_PUBKEY=$(echo 11111111111111111111111111111111)
   SPL_TOKEN_PROGRAM_PUBKEY=$(echo TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA)
      SPLATA_PROGRAM_PUBKEY=$(echo ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL)
       STAKE_PROGRAM_PUBKEY=$(echo Stake11111111111111111111111111111111111111)
        CLOCK_SYSVAR_PUBKEY=$(echo SysvarC1ock11111111111111111111111111111111)
         RENT_SYSVAR_PUBKEY=$(echo SysvarRent111111111111111111111111111111111)
    METAPLEX_PROGRAM_PUBKEY=$(echo metaqbxxUerdq28cj1RbAWkYQm3ybzjb6a8bt518x1s)
STAKE_HISTORY_SYSVAR_PUBKEY=$(echo SysvarStakeHistory1111111111111111111111111)
 STAKE_CONFIG_SYSVAR_PUBKEY=$(echo StakeConfig11111111111111111111111111111111)
        SELF_PROGRAM_PUBKEY=$(echo Shin1cdrR1pmemXZU3yDV3PnQ48SX9UmrtHF4GbKzWG)
              CONFIG_PUBKEY=$(pda $SELF_PROGRAM_PUBKEY u8[1])
           AUTHORITY_PUBKEY=$(pda $SELF_PROGRAM_PUBKEY u8[2])
        MASTER_STAKE_PUBKEY=$(pda $SELF_PROGRAM_PUBKEY u8[3])
             KI_MINT_PUBKEY=$(pda $SELF_PROGRAM_PUBKEY u8[4])
     BID_MARKER_MINT_PUBKEY=$(pda $SELF_PROGRAM_PUBKEY u8[11])
         KI_METADATA_PUBKEY=$(pda $METAPLEX_PROGRAM_PUBKEY                                                            \
                                  String[metadata]                                                                    \
                                  Pubkey[$METAPLEX_PROGRAM_PUBKEY]                                                    \
                                  Pubkey[$KI_MINT_PUBKEY])
 BID_MARKER_METADATA_PUBKEY=$(pda $METAPLEX_PROGRAM_PUBKEY                                                            \
                                  String[metadata]                                                                    \
                                  Pubkey[$METAPLEX_PROGRAM_PUBKEY]                                                    \
                                  Pubkey[$BID_MARKER_MINT_PUBKEY])

# Compute block pubkey
               BLOCK_PUBKEY=$(pda $SELF_PROGRAM_PUBKEY                                                                \
                                  u8[14]                                                                              \
                                  u32[$GROUP_NUMBER]                                                                  \
                                  u32[$BLOCK_NUMBER])

solxact encode                                                                                                        \
        encoding c                                                                                                    \
        fee_payer $ADMIN_PUBKEY                                                                                       \
        program $SELF_PROGRAM_PUBKEY                                                                                  \
        account $CONFIG_PUBKEY                                                                                        \
        account $ADMIN_PUBKEY s                                                                                       \
        account $ADMIN_PUBKEY ws                                                                                      \
        account $BLOCK_PUBKEY w                                                                                       \
        account $SYSTEM_PROGRAM_PUBKEY                                                                                \
        // Instruction code 2 = CreateBlock //                                                                        \
        u8 2                                                                                                          \
        u16 $COMMISSION                                                                                               \
        // Block Configuration //                                                                                     \
        struct [                                                                                                      \
        u32 $GROUP_NUMBER                                                                                             \
        u32 $BLOCK_NUMBER                                                                                             \
        u16 $TOTAL_ENTRY_COUNT                                                                                        \
        u16 $TOTAL_MYSTERY_COUNT                                                                                      \
        u32 $MYSTERY_PHASE_DURATION                                                                                   \
        u64 $MYSTERY_START_PRICE_LAMPORTS                                                                             \
        u32 $REVEAL_PERIOD_DURATION                                                                                   \
        u64 $MINIMUM_PRICE_LAMPORTS                                                                                   \
        bool $HAS_AUCTION                                                                                             \
        u32 $DURATION                                                                                                 \
        u64 $FINAL_START_PRICE_LAMPORTS                                                                               \
        u32 $WHITELIST_DURATION                                                                                       \
        ]
