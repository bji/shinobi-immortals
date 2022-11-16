#!/bin/sh

set -e

# Emits an encoded transaction that performs reauthorize on an entry.

function require ()
{
    if [ -z "$1" ]; then
        cat <<EOF

Usage: special_reauthorize_tx.sh <FEE_PAYER_PUBKEY> <ADMIN_PUBKEY> <USER_PUBKEY> <GROUP_NUMBER> <BLOCK_NUMBER> \\
                                 <ENTRY_INDEX> <ENTRY_STAKE_ACCOUNT_PUBKEY> <NEW_AUTHORITY_PUBKEY> [<TOKEN_PUBKEY>]

If <TOKEN_PUBKEY> is not provided, it is assumed to be the Associated Token Account.
If the entry is not staked, then any key can be passed in for <ENTRY_STAKE_ACCOUNT_PUBKEY>, it will be ignored.

EOF
        exit 1
    fi
}

function pda ()
{
    solpda $@ | cut -d . -f 1
}

FEE_PAYER_PUBKEY=$1
ADMIN_PUBKEY=$2
USER_PUBKEY=$3
GROUP_NUMBER=$4
BLOCK_NUMBER=$5
ENTRY_INDEX=$6
ENTRY_STAKE_ACCOUNT_PUBKEY=$7
NEW_AUTHORITY_PUBKEY=$8
TOKEN_PUBKEY=$9

require $FEE_PAYER_PUBKEY
require $ADMIN_PUBKEY
require $USER_PUBKEY
require $GROUP_NUMBER
require $BLOCK_NUMBER
require $ENTRY_INDEX
require $ENTRY_STAKE_ACCOUNT_PUBKEY
require $NEW_AUTHORITY_PUBKEY

if [ -e "$FEE_PAYER_PUBKEY" ]; then
    FEE_PAYER_PUBKEY=$(solpda -pubkey "$FEE_PAYER_PUBKEY")
fi

if [ -e "$ADMIN_PUBKEY" ]; then
    ADMIN_PUBKEY=$(solpda -pubkey "$ADMIN_PUBKEY")
fi

if [ -e "$USER_PUBKEY" ]; then
    USER_PUBKEY=$(solpda -pubkey "$USER_PUBKEY")
fi

if [ -e "$NEW_AUTHORITY_PUBKEY" ]; then
    NEW_AUTHORITY_PUBKEY=$(solpda -pubkey "$NEW_AUTHORITY_PUBKEY")
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
      ENTRY_METADATA_PUBKEY=$(pda $METAPLEX_PROGRAM_PUBKEY                                                            \
                                  String[metadata]                                                                    \
                                  Pubkey[$METAPLEX_PROGRAM_PUBKEY]                                                    \
                                  Pubkey[$ENTRY_MINT_PUBKEY])
if [ -z "$TOKEN_PUBKEY" ]; then 
               TOKEN_PUBKEY=$(pda $SPLATA_PROGRAM_PUBKEY                                                              \
                                  Pubkey[$USER_PUBKEY]                                                                \
                                  Pubkey[$SPL_TOKEN_PROGRAM_PUBKEY]                                                   \
                                  Pubkey[$ENTRY_MINT_PUBKEY])
fi
              
solxact encode                                                                                                        \
        encoding c                                                                                                    \
        fee_payer $FEE_PAYER_PUBKEY                                                                                   \
        program $SELF_PROGRAM_PUBKEY                                                                                  \
        account $CONFIG_PUBKEY                                                                                        \
        account $ADMIN_PUBKEY s                                                                                       \
        account $ENTRY_PUBKEY w                                                                                       \
        account $USER_PUBKEY s                                                                                        \
        account $TOKEN_PUBKEY                                                                                         \
        account $ENTRY_METADATA_PUBKEY w                                                                              \
        account $ENTRY_STAKE_ACCOUNT_PUBKEY w                                                                         \
        account $AUTHORITY_PUBKEY                                                                                     \
        account $CLOCK_SYSVAR_PUBKEY                                                                                  \
        account $METAPLEX_PROGRAM_PUBKEY                                                                              \
        account $STAKE_PROGRAM_PUBKEY                                                                                 \
        // Instruction code 20 = Reauthorize //                                                                       \
        u8 20                                                                                                         \
        pubkey $NEW_AUTHORITY_PUBKEY
