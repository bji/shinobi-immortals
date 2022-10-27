#!/bin/sh

set -e

# Emits an encoded transaction that sets metadata bytes within an entry.  Assumes that admin is the funding_account.

function require ()
{
    if [ -z "$1" ]; then
        cat <<EOF

Usage: admin_set_metadata_bytes_tx.sh <ADMIN_PUBKEY> <GROUP_NUMBER> <BLOCK_NUMBER> <ENTRY_INDEX> \\
                                      <DATA_START_INDEX> <BASE64_ENCODED_DATA>

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
ENTRY_INDEX=$4
DATA_START_INDEX=$5
BASE64_ENCODED_DATA=$6

require $ADMIN_PUBKEY
require $GROUP_NUMBER
require $BLOCK_NUMBER
require $ENTRY_INDEX
require $DATA_START_INDEX
require $BASE64_ENCODED_DATA

# Convert BASE64_ENCODED_BYTES into METADATA_BYTES
METADATA_BYTES=$(echo $BASE64_ENCODED_DATA | base64 -d | od -An -tu1 | tr -d '\n' | tr -s '[:space:]')
METADATA_BYTES_COUNT=$(echo $BASE64_ENCODED_DATA | base64 -d | wc -c)

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

# Compute block, entry mint, and entry pubkeys
               BLOCK_PUBKEY=$(pda $SELF_PROGRAM_PUBKEY                                                                \
                                  u8[14]                                                                              \
                                  u32[$GROUP_NUMBER]                                                                  \
                                  u32[$BLOCK_NUMBER])
          ENTRY_MINT_PUBKEY=$(pda $SELF_PROGRAM_PUBKEY                                                                \
                                  u8[5]                                                                               \
                                  Pubkey[$BLOCK_PUBKEY]                                                               \
                                  u16[$ENTRY_INDEX])
              ENTRY_PUBKEY=$(pda $SELF_PROGRAM_PUBKEY                                                                 \
                                 u8[15]                                                                               \
                                 Pubkey[$ENTRY_MINT_PUBKEY])

solxact encode                                                                                                        \
        encoding c                                                                                                    \
        fee_payer $ADMIN_PUBKEY                                                                                       \
        program $SELF_PROGRAM_PUBKEY                                                                                  \
        account $CONFIG_PUBKEY                                                                                        \
        account $ADMIN_PUBKEY s                                                                                       \
        account $BLOCK_PUBKEY                                                                                         \
        account $ENTRY_PUBKEY w                                                                                       \
        // Instruction code 4 = SetMetadataBytesData //                                                               \
        u8 4                                                                                                          \
        u16 $DATA_START_INDEX                                                                                         \
        u16 $METADATA_BYTES_COUNT                                                                                     \
        u8 $METADATA_BYTES
