#!/bin/sh

set -e

# Emits an encoded transaction that sets the Shinobi Immortals admin pubkey for a cluster

function require ()
{
    if [ -z "$1" ]; then
        cat <<EOF

Usage: super_set_admin_tx.sh <SUPERUSER_PUBKEY> <NEW_ADMIN_PUBKEY>

EOF
        exit 1
    fi
}

function pda ()
{
    solpda $@ | cut -d . -f 1
}

SUPERUSER_PUBKEY=$1
NEW_ADMIN_PUBKEY=$2

require $SUPERUSER_PUBKEY
require $NEW_ADMIN_PUBKEY

if [ -e "$SUPERUSER_PUBKEY" ]; then
    SUPERUSER_PUBKEY=$(solpda -pubkey "$SUPERUSER_PUBKEY")
fi

if [ -e "$NEW_ADMIN_PUBKEY" ]; then
    NEW_ADMIN_PUBKEY=$(solpda -pubkey "$NEW_ADMIN_PUBKEY")
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

solxact encode                                                                                                        \
        encoding c                                                                                                    \
        fee_payer $SUPERUSER_PUBKEY                                                                                   \
        program $SELF_PROGRAM_PUBKEY                                                                                  \
        account $SUPERUSER_PUBKEY s                                                                                   \
        account $CONFIG_PUBKEY w                                                                                      \
        // Instruction code 1 = UpdateAdmin //                                                                        \
        u8 1                                                                                                          \
        // Admin pubkey //                                                                                            \
        pubkey $NEW_ADMIN_PUBKEY
