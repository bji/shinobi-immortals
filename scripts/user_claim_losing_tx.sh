#!/bin/sh

set -e

# Emits an encoded transaction that claims a losing bid.

function require ()
{
    if [ -z "$1" ]; then
        cat <<EOF

Usage: user_claim_losing_tx.sh <USER_PUBKEY> <GROUP_NUMBER> <BLOCK_NUMBER> <ENTRY_INDEX>


EOF
        exit 1
    fi
}

function pda ()
{
    solpda $@ | cut -d . -f 1
}

USER_PUBKEY=$1
GROUP_NUMBER=$2
BLOCK_NUMBER=$3
ENTRY_INDEX=$4

require $USER_PUBKEY
require $GROUP_NUMBER
require $BLOCK_NUMBER
require $ENTRY_INDEX

# Derive user pubkey from user key file
if [ -e "$USER_PUBKEY" ]; then
    USER_PUBKEY=$(solpda -pubkey $USER_PUBKEY)
fi

if [ -z "$SELF_PROGRAM_PUBKEY" ]; then
    SELF_PROGRAM_PUBKEY=$(echo Shin1cdrR1pmemXZU3yDV3PnQ48SX9UmrtHF4GbKzWG)
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
        STAKE_CONFIG_PUBKEY=$(echo StakeConfig11111111111111111111111111111111)
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
    BID_MARKER_TOKEN_PUBKEY=$(pda $SELF_PROGRAM_PUBKEY                                                                \
                                  u8[12]                                                                              \
                                  Pubkey[$ENTRY_MINT_PUBKEY]                                                          \
                                  Pubkey[$USER_PUBKEY])
                 BID_PUBKEY=$(pda $SELF_PROGRAM_PUBKEY                                                                \
                                  u8[9]                                                                               \
                                  Pubkey[$BID_MARKER_TOKEN_PUBKEY])
                                  
solxact encode                                                                                                        \
        encoding c                                                                                                    \
        fee_payer $USER_PUBKEY                                                                                        \
        program $SELF_PROGRAM_PUBKEY                                                                                  \
        account $USER_PUBKEY ws                                                                                       \
        account $ENTRY_PUBKEY                                                                                         \
        account $BID_PUBKEY w                                                                                         \
        // Instruction code 13 = ClaimLosing //                                                                       \
        u8 13
