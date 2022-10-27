#!/bin/sh

set -e

# Emits an encoded transaction that performs a take commission/delegate action on an entry.

function require ()
{
    if [ -z "$1" ]; then
        cat <<EOF

Usage: anyone_take_commission_or_delegate_tx.sh <FEE_PAYER_PUBKEY> <GROUP_NUMBER> <BLOCK_NUMBER> <ENTRY_INDEX> \\
                                                <ENTRY_STAKE_ACCOUNT_PUBKEY> [<MINIMUM_STAKE_LAMPORTS>]

EOF
        exit 1
    fi
}

function pda ()
{
    solpda $@ | cut -d . -f 1
}

FEE_PAYER_PUBKEY=$1
GROUP_NUMBER=$2
BLOCK_NUMBER=$3
ENTRY_INDEX=$4
ENTRY_STAKE_ACCOUNT_PUBKEY=$5
MINIMUM_STAKE_LAMPORTS=$6

require $FEE_PAYER_PUBKEY
require $GROUP_NUMBER
require $BLOCK_NUMBER
require $ENTRY_INDEX
require $ENTRY_STAKE_ACCOUNT_PUBKEY

if [ -e "$FEE_PAYER_PUBKEY" ]; then
    FEE_PAYER_PUBKEY=$(solpda -pubkey "$FEE_PAYER_PUBKEY")
fi

if [ -z "$MINIMUM_STAKE_LAMPORTS" ]; then
    MINIMUM_STAKE_LAMPORTS=0
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

# Compute block and entry related pubkeys
 
               BLOCK_PUBKEY=$(pda $SELF_PROGRAM_PUBKEY                                                                \
                                  u8[14]                                                                              \
                                  u32[$GROUP_NUMBER]                                                                  \
                                  u32[$BLOCK_NUMBER])
          ENTRY_MINT_PUBKEY=$(pda $SELF_PROGRAM_PUBKEY                                                                \
                                  u8[5]                                                                               \
                                  Pubkey[$BLOCK_PUBKEY]                                                               \
                                  u16[$ENTRY_INDEX])
               ENTRY_PUBKEY=$(pda $SELF_PROGRAM_PUBKEY                                                                \
                                  u8[15]                                                                              \
                                  Pubkey[$ENTRY_MINT_PUBKEY])
      ENTRY_METADATA_PUBKEY=$(pda $METAPLEX_PROGRAM_PUBKEY                                                            \
                                  String[metadata]                                                                    \
                                  Pubkey[$METAPLEX_PROGRAM_PUBKEY]                                                    \
                                  Pubkey[$ENTRY_MINT_PUBKEY])
              BRIDGE_PUBKEY=$(pda $SELF_PROGRAM_PUBKEY                                                                \
                                  u8[10]                                                                              \
                                  Pubkey[$ENTRY_MINT_PUBKEY])
              
solxact encode                                                                                                        \
        encoding c                                                                                                    \
        fee_payer $FEE_PAYER_PUBKEY                                                                                   \
        program $SELF_PROGRAM_PUBKEY                                                                                  \
        account $FEE_PAYER_PUBKEY ws                                                                                  \
        account $BLOCK_PUBKEY                                                                                         \
        account $ENTRY_PUBKEY w                                                                                       \
        account $ENTRY_STAKE_ACCOUNT_PUBKEY w                                                                         \
        account $MASTER_STAKE_PUBKEY w                                                                                \
        account $BRIDGE_PUBKEY w                                                                                      \
        account $AUTHORITY_PUBKEY                                                                                     \
        account $CLOCK_SYSVAR_PUBKEY                                                                                  \
        account $SYSTEM_PROGRAM_PUBKEY                                                                                \
        account $STAKE_PROGRAM_PUBKEY                                                                                 \
        account $STAKE_HISTORY_SYSVAR_PUBKEY                                                                          \
        // Instruction code 19 = TakeCommissionOrDelegate //                                                          \
        u8 19                                                                                                         \
        u64 $MINIMUM_STAKE_LAMPORTS
