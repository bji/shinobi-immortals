#!/bin/sh

set -e

# Emits an encoded transaction that adds entries to a block.  Assumes that admin is the funding_account.

function require ()
{
    if [ -z "$1" ]; then
        cat <<EOF

Usage: admin_add_entries_to_block_tx.sh <ADMIN_PUBKEY> <GROUP_NUMBER> <BLOCK_NUMBER> <METAPLEX_METADATA_URI> \\
                                        <SECOND_METAPLEX_METADATA_CREATOR or "none"> <FIRST_ENTRY_INDEX> \\
                                        <SHA_256>...

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
METAPLEX_METADATA_URI=$4
SECOND_METAPLEX_METADATA_CREATOR_OR_NONE=$5
FIRST_ENTRY_INDEX=$6

# Compose SHA-256 pubkey data elements from the remaining arguments
SHA256_DATA=
while [ -n "$7" ]; do
    SHA256_DATA="$SHA256_DATA $(echo "$7" | xxd -r -p | od -An -tu1 | tr -d '\n' | tr -s '[:space:]')"
    shift
done

require $ADMIN_PUBKEY
require $GROUP_NUMBER
require $BLOCK_NUMBER
require $METAPLEX_METADATA_URI
require $SECOND_METAPLEX_METADATA_CREATOR_OR_NONE
require $FIRST_ENTRY_INDEX
require $SHA256_DATA

if [ -e "$ADMIN_PUBKEY" ]; then
    ADMIN_PUBKEY=$(solpda -pubkey "$ADMIN_PUBKEY")
fi

if [ "$SECOND_METAPLEX_METADATA_CREATOR_OR_NONE" = "none" ]; then
    SECOND_METAPLEX_METADATA_CREATOR_OR_NONE=$(echo 11111111111111111111111111111111)
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
        account $AUTHORITY_PUBKEY                                                                                     \
        account $SYSTEM_PROGRAM_PUBKEY                                                                                \
        account $SPL_TOKEN_PROGRAM_PUBKEY                                                                             \
        account $METAPLEX_PROGRAM_PUBKEY                                                                              \
        account $RENT_SYSVAR_PUBKEY                                                                                   \
        // Instruction code 3 = AddEntriesToBlockData //                                                              \
        u8 3                                                                                                          \
        c_string 200 $METAPLEX_METADATA_URI                                                                           \
        pubkey $SECOND_METAPLEX_METADATA_CREATOR_OR_NONE                                                              \
        u16 $FIRST_ENTRY_INDEX                                                                                        \
        u8 $SHA256_DATA
