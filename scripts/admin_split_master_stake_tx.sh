#!/bin/sh

set -e

# Emits an encoded transaction that splits all stake above the master stake minimum off of the master stake account.
# Assumes that admin is the funding_account.

function require ()
{
    if [ -z "$1" ]; then
        cat <<EOF

Usage: admin_split_master_stake_tx.sh <ADMIN_PUBKEY> <INTO_STAKE_PUBKEY> <LAMPORTS_TO_SPLIT> [<PRE_MERGE_STAKE_PUBKEY>]

EOF
        exit 1
    fi
}

function pda ()
{
    solpda $@ | cut -d . -f 1
}

ADMIN_PUBKEY=$1
INTO_STAKE_PUBKEY=$2
LAMPORTS_TO_SPLIT=$3
PRE_MERGE_STAKE_PUBKEY=$4

require $ADMIN_PUBKEY
require $INTO_STAKE_PUBKEY
require $LAMPORTS_TO_SPLIT

if [ -e "$ADMIN_PUBKEY" ]; then
    ADMIN_PUBKEY=$(solpda -pubkey "$ADMIN_PUBKEY")
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

 
if [ -n "$PRE_MERGE_STAKE_PUBKEY" ]; then
    PRE_MERGE_STAKE_PUBKEY="$PRE_MERGE_STAKE_PUBKEY w"
else
    PRE_MERGE_STAKE_PUBKEY=$SYSTEM_PROGRAM_PUBKEY
fi


solxact encode                                                                                                        \
        encoding c                                                                                                    \
        fee_payer $ADMIN_PUBKEY                                                                                       \
        program $SELF_PROGRAM_PUBKEY                                                                                  \
        account $CONFIG_PUBKEY                                                                                        \
        account $ADMIN_PUBKEY s                                                                                       \
        account $MASTER_STAKE_PUBKEY w                                                                                \
        account $INTO_STAKE_PUBKEY ws                                                                                 \
        account $PRE_MERGE_STAKE_PUBKEY                                                                               \
        account $AUTHORITY_PUBKEY                                                                                     \
        account $CLOCK_SYSVAR_PUBKEY                                                                                  \
        account $SYSTEM_PROGRAM_PUBKEY                                                                                \
        account $STAKE_PROGRAM_PUBKEY                                                                                 \
        account $STAKE_HISTORY_SYSVAR_PUBKEY                                                                          \
        // Instruction code 7 = SplitMasterStake //                                                                   \
        u8 7                                                                                                          \
        u64 $LAMPORTS_TO_SPLIT                                                                                        
