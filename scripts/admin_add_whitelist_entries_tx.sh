#!/bin/sh

set -e

# Emits an encoded transaction that adds whitelist entries to the whitelist of a block.  Assumes that admin is the
# funding_account.

function require ()
{
    if [ -z "$1" ]; then
        cat <<EOF

Usage: admin_add_whitelist_entries_tx.sh <ADMIN_PUBKEY> <GROUP_NUMBER> <BLOCK_NUMBER> <PUBKEY_TO_WHITELIST>...

EOF
        exit 1
    fi
}

ADMIN_PUBKEY=$1
GROUP_NUMBER=$2
BLOCK_NUMBER=$3

# Compose whitelist pubkey list
WHITELIST_PUBKEYS=
WHITELIST_PUBKEYS_COUNT=0
while [ -n "$4" ]; do
    WHITELIST_PUBKEYS="$WHITELIST_PUBKEYS pubkey $4"
    WHITELIST_PUBKEYS_COUNT=$(($WHITELIST_PUBKEYS_COUNT+1))
    shift
done

require $ADMIN_PUBKEY
require $GROUP_NUMBER
require $BLOCK_NUMBER
require $WHITELIST_PUBKEYS

# Compute program, block, entry, and related pubkeys.  These may not all be valid, depending on input parameters
# to the script, but any that are invalid won't be used by the solxact encode command that follows.

if [ -z "$SELF_PROGRAM_PUBKEY" ]; then
        SELF_PROGRAM_PUBKEY="Shin1cdrR1pmemXZU3yDV3PnQ48SX9UmrtHF4GbKzWG"
fi
if [ -z $SHINOBI_SYSTEMS_VOTE_PUBKEY ]; then
SHINOBI_SYSTEMS_VOTE_PUBKEY="BLADE1qNA1uNjRgER6DtUFf7FU3c1TWLLdpPeEcKatZ2"
fi
      SYSTEM_PROGRAM_PUBKEY="11111111111111111111111111111111"
   SPL_TOKEN_PROGRAM_PUBKEY="TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA"
      SPLATA_PROGRAM_PUBKEY="ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL"
       STAKE_PROGRAM_PUBKEY="Stake11111111111111111111111111111111111111"
        CLOCK_SYSVAR_PUBKEY="SysvarC1ock11111111111111111111111111111111"
         RENT_SYSVAR_PUBKEY="SysvarRent111111111111111111111111111111111"
    METAPLEX_PROGRAM_PUBKEY="metaqbxxUerdq28cj1RbAWkYQm3ybzjb6a8bt518x1s"
STAKE_HISTORY_SYSVAR_PUBKEY="SysvarStakeHistory1111111111111111111111111"
        STAKE_CONFIG_PUBKEY="StakeConfig11111111111111111111111111111111"
              CONFIG_PUBKEY="pda $SELF_PROGRAM_PUBKEY [ u8 1 ]"
           AUTHORITY_PUBKEY="pda $SELF_PROGRAM_PUBKEY [ u8 2 ]"
        MASTER_STAKE_PUBKEY="pda $SELF_PROGRAM_PUBKEY [ u8 3 ]"
             KI_MINT_PUBKEY="pda $SELF_PROGRAM_PUBKEY [ u8 4 ]"
     BID_MARKER_MINT_PUBKEY="pda $SELF_PROGRAM_PUBKEY [ u8 11 ]"
         KI_METADATA_PUBKEY="pda $METAPLEX_PROGRAM_PUBKEY                                                             \
                                 [ string metadata                                                                    \
                                   pubkey $METAPLEX_PROGRAM_PUBKEY                                                    \
                                   $KI_MINT_PUBKEY ]"
 BID_MARKER_METADATA_PUBKEY="pda $METAPLEX_PROGRAM_PUBKEY                                                             \
                                 [ string metadata                                                                    \
                                   pubkey $METAPLEX_PROGRAM_PUBKEY                                                    \
                                   $BID_MARKER_MINT_PUBKEY ]"
               BLOCK_PUBKEY="pda $SELF_PROGRAM_PUBKEY                                                                 \
                                 [ u8 14                                                                              \
                                   u32 $GROUP_NUMBER                                                                  \
                                   u32 $BLOCK_NUMBER ]"
           WHITELIST_PUBKEY="pda $SELF_PROGRAM_PUBKEY                                                                 \
                                 [ u8 13                                                                              \
                                   $BLOCK_PUBKEY ]"
          ENTRY_MINT_PUBKEY="pda $SELF_PROGRAM_PUBKEY                                                                 \
                                 [ u8 5                                                                               \
                                   $BLOCK_PUBKEY                                                                      \
                                   u16 $ENTRY_INDEX ]"
               ENTRY_PUBKEY="pda $SELF_PROGRAM_PUBKEY                                                                 \
                                 [ u8 15                                                                              \
                                   $ENTRY_MINT_PUBKEY ]"
         ENTRY_TOKEN_PUBKEY="pda $SELF_PROGRAM_PUBKEY                                                                 \
                                 [ u8 6                                                                               \
                                   $ENTRY_MINT_PUBKEY ]"
      ENTRY_METADATA_PUBKEY="pda $METAPLEX_PROGRAM_PUBKEY                                                             \
                                 [ string metadata                                                                    \
                                   pubkey $METAPLEX_PROGRAM_PUBKEY                                                    \
                                   $ENTRY_MINT_PUBKEY ]"
   TOKEN_DESTINATION_PUBKEY="pda $SPLATA_PROGRAM_PUBKEY                                                               \
                                 [ pubkey $USER_PUBKEY                                                                \
                                   pubkey $SPL_TOKEN_PROGRAM_PUBKEY                                                   \
                                   $ENTRY_MINT_PUBKEY ]"
    BID_MARKER_TOKEN_PUBKEY="pda $SELF_PROGRAM_PUBKEY                                                                 \
                                 [ u8 12                                                                              \
                                   $ENTRY_MINT_PUBKEY                                                                 \
                                   pubkey $USER_PUBKEY ]"
                 BID_PUBKEY="pda $SELF_PROGRAM_PUBKEY                                                                 \
                                 [ u8 9                                                                               \
                                   $BID_MARKER_TOKEN_PUBKEY ]"
               
solxact encode                                                                                                        \
        encoding c                                                                                                    \
        fee_payer $ADMIN_PUBKEY                                                                                       \
        program $SELF_PROGRAM_PUBKEY                                                                                  \
        account $CONFIG_PUBKEY                                                                                        \
        account $ADMIN_PUBKEY s                                                                                       \
        account $ADMIN_PUBKEY ws                                                                                      \
        account $BLOCK_PUBKEY                                                                                         \
        account $WHITELIST_PUBKEY w                                                                                   \
        account $SYSTEM_PROGRAM_PUBKEY                                                                                \
        // Instruction code 8 = AddWhitelistEntries //                                                                \
        u8 8                                                                                                          \
        u16 $WHITELIST_PUBKEYS_COUNT                                                                                  \
        $WHITELIST_PUBKEYS
