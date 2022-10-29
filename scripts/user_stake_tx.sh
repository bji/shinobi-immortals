#!/bin/sh

set -e

# Emits an encoded transaction that performs an entry stake.  The stake account withdrawal authority must be the
# user pubkey.

function require ()
{
    if [ -z "$1" ]; then
        cat <<EOF

Usage: user_stake_tx.sh <USER_PUBKEY> <GROUP_NUMBER> <BLOCK_NUMBER> <ENTRY_INDEX> <STAKE_ACCOUNT_PUBKEY> \\
                        [<TOKEN_PUBKEY>]

If <TOKEN_PUBKEY> is not provided, it is assumed to be the Associated Token Account.

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
STAKE_ACCOUNT_PUBKEY=$5
TOKEN_PUBKEY=$6

require $USER_PUBKEY
require $GROUP_NUMBER
require $BLOCK_NUMBER
require $ENTRY_INDEX
require $STAKE_ACCOUNT_PUBKEY

if [ -e "$USER_PUBKEY" ]; then
    USER_PUBKEY=$(solpda -pubkey "$USER_PUBKEY")
fi

if [ -e "$STAKE_ACCOUNT_PUBKEY" ]; then
    STAKE_ACCOUNT_PUBKEY=$(solpda -pubkey "$STAKE_ACCOUNT_PUBKEY")
fi

if [ -z $SHINOBI_SYSTEMS_VOTE_PUBKEY ]; then
    SHINOBI_SYSTEMS_VOTE_PUBKEY=$(echo BLADE1qNA1uNjRgER6DtUFf7FU3c1TWLLdpPeEcKatZ2)
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
if [ -z "$TOKEN_PUBKEY" ]; then 
               TOKEN_PUBKEY=$(pda $SPLATA_PROGRAM_PUBKEY                                                              \
                                  Pubkey[$USER_PUBKEY]                                                                \
                                  Pubkey[$SPL_TOKEN_PROGRAM_PUBKEY]                                                   \
                                  Pubkey[$ENTRY_MINT_PUBKEY])
fi
                                  
solxact encode                                                                                                        \
        encoding c                                                                                                    \
        fee_payer $USER_PUBKEY                                                                                        \
        program $SELF_PROGRAM_PUBKEY                                                                                  \
        account $BLOCK_PUBKEY                                                                                         \
        account $ENTRY_PUBKEY w                                                                                       \
        account $USER_PUBKEY s                                                                                        \
        account $TOKEN_PUBKEY                                                                                         \
        account $STAKE_ACCOUNT_PUBKEY w                                                                               \
        account $USER_PUBKEY s                                                                                        \
        account $SHINOBI_SYSTEMS_VOTE_PUBKEY                                                                          \
        account $AUTHORITY_PUBKEY                                                                                     \
        account $CLOCK_SYSVAR_PUBKEY                                                                                  \
        account $STAKE_PROGRAM_PUBKEY                                                                                 \
        account $STAKE_CONFIG_PUBKEY                                                                                  \
        account $STAKE_HISTORY_SYSVAR_PUBKEY                                                                          \
        // Instruction code 15 = Stake //                                                                             \
        u8 15
