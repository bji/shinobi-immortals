#!/bin/sh

set -e

# Emits an encoded transaction that claims a winning bid.  Assumes that the user account is the token destination.

function require ()
{
    if [ -z "$1" ]; then
        cat <<EOF

Usage: user_claim_winning_tx.sh <ADMIN_PUBKEY> <USER_PUBKEY> <GROUP_NUMBER> <BLOCK_NUMBER> <ENTRY_INDEX>

EOF
        exit 1
    fi
}

function pda ()
{
    solpda $@ | cut -d . -f 1
}

ADMIN_PUBKEY=$1
USER_PUBKEY=$2
GROUP_NUMBER=$3
BLOCK_NUMBER=$4
ENTRY_INDEX=$5

require $ADMIN_PUBKEY
require $USER_PUBKEY
require $GROUP_NUMBER
require $BLOCK_NUMBER
require $ENTRY_INDEX

if [ -e "$ADMIN_PUBKEY" ]; then
    ADMIN_PUBKEY=$(solpda -pubkey "$ADMIN_PUBKEY")
fi

if [ -e "$USER_PUBKEY" ]; then
    USER_PUBKEY=$(solpda -pubkey "$USER_PUBKEY")
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
         ENTRY_TOKEN_PUBKEY=$(pda $SELF_PROGRAM_PUBKEY                                                                \
                                  u8[6]                                                                               \
                                  Pubkey[$ENTRY_MINT_PUBKEY])
    BID_MARKER_TOKEN_PUBKEY=$(pda $SELF_PROGRAM_PUBKEY                                                                \
                                  u8[12]                                                                              \
                                  Pubkey[$ENTRY_MINT_PUBKEY]                                                          \
                                  Pubkey[$USER_PUBKEY])
                 BID_PUBKEY=$(pda $SELF_PROGRAM_PUBKEY                                                                \
                                  u8[9]                                                                               \
                                  Pubkey[$BID_MARKER_TOKEN_PUBKEY])
   TOKEN_DESTINATION_PUBKEY=$(pda $SPLATA_PROGRAM_PUBKEY                                                              \
                                  Pubkey[$USER_PUBKEY]                                                                \
                                  Pubkey[$SPL_TOKEN_PROGRAM_PUBKEY]                                                   \
                                  Pubkey[$ENTRY_MINT_PUBKEY])
                                  
solxact encode                                                                                                        \
        encoding c                                                                                                    \
        fee_payer $USER_PUBKEY                                                                                        \
        program $SELF_PROGRAM_PUBKEY                                                                                  \
        account $USER_PUBKEY ws                                                                                       \
        account $ENTRY_PUBKEY w                                                                                       \
        account $BID_PUBKEY w                                                                                         \
        account $CONFIG_PUBKEY                                                                                        \
        account $ADMIN_PUBKEY w                                                                                       \
        account $ENTRY_TOKEN_PUBKEY w                                                                                 \
        account $ENTRY_MINT_PUBKEY                                                                                    \
        account $AUTHORITY_PUBKEY                                                                                     \
        account $TOKEN_DESTINATION_PUBKEY w                                                                           \
        account $USER_PUBKEY                                                                                          \
        account $SYSTEM_PROGRAM_PUBKEY                                                                                \
        account $SPL_TOKEN_PROGRAM_PUBKEY                                                                             \
        account $SPLATA_PROGRAM_PUBKEY                                                                                \
        // Instruction code 14 = ClaimWinning //                                                                      \
        u8 14
