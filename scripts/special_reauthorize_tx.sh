#!/bin/bash

set -e

# Emits an encoded transaction that performs reauthorize on an entry.

function require ()
{
    if [ -z "$1" ]; then
        cat <<EOF

Usage: special_reauthorize_tx.sh <FEE_PAYER_PUBKEY> <ADMIN_PUBKEY> <USER_PUBKEY> <GROUP_NUMBER> <BLOCK_NUMBER> \\
                                 <ENTRY_INDEX> <ENTRY_STAKE_ACCOUNT_PUBKEY> <NEW_METADATA_AUTHORITY_PUBKEY> \\
                                 <REAUTHORIZE_METADATA> [<TOKEN_PUBKEY>]

If the entry is staked, its stake account is returned to <USER_PUBKEY>, without harvesting Ki or charging
commission.  It cannot be sent anywhere else.
<REAUTHORIZE_METADATA> is "true" to also set the Metaplex metadata update authority of the entry to
<NEW_METADATA_AUTHORITY_PUBKEY>, or "false" to only return the entry's stake account without invoking the Metaplex
Metadata program at all.  "false" is only valid for a staked entry, and <NEW_METADATA_AUTHORITY_PUBKEY> is then
ignored.
If <TOKEN_PUBKEY> is not provided, it is assumed to be the Associated Token Account.
If the entry is not staked, then any key can be passed in for <ENTRY_STAKE_ACCOUNT_PUBKEY>, it will be ignored.

EOF
        exit 1
    fi
}

FEE_PAYER_PUBKEY=$1
ADMIN_PUBKEY=$2
USER_PUBKEY=$3
GROUP_NUMBER=$4
BLOCK_NUMBER=$5
ENTRY_INDEX=$6
ENTRY_STAKE_ACCOUNT_PUBKEY=$7
NEW_METADATA_AUTHORITY_PUBKEY=$8
REAUTHORIZE_METADATA=$9
TOKEN_PUBKEY=${10}

require $FEE_PAYER_PUBKEY
require $ADMIN_PUBKEY
require $USER_PUBKEY
require $GROUP_NUMBER
require $BLOCK_NUMBER
require $ENTRY_INDEX
require $ENTRY_STAKE_ACCOUNT_PUBKEY
require $NEW_METADATA_AUTHORITY_PUBKEY
require $REAUTHORIZE_METADATA

# The reauthorize metadata flag must be exactly "true" or "false"; anything else is a usage error
case "$REAUTHORIZE_METADATA" in
    true|false) ;;
    *) require "" ;;
esac

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
              STATE_PUBKEY="pda $SELF_PROGRAM_PUBKEY [ u8 1 ]"
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
if [ -z "$TOKEN_PUBKEY" ]; then 
               TOKEN_PUBKEY="pda $SPLATA_PROGRAM_PUBKEY                                                               \
                                 [ pubkey $USER_PUBKEY                                                                \
                                   pubkey $SPL_TOKEN_PROGRAM_PUBKEY                                                   \
                                   $ENTRY_MINT_PUBKEY ]"
fi

solxact encode                                                                                                        \
        encoding c                                                                                                    \
        fee_payer $FEE_PAYER_PUBKEY                                                                                   \
        program $SELF_PROGRAM_PUBKEY                                                                                  \
        account $STATE_PUBKEY                                                                                        \
        account $ADMIN_PUBKEY s                                                                                       \
        account $ENTRY_PUBKEY w                                                                                       \
        account $USER_PUBKEY s                                                                                        \
        // The token account is writable because it is thawed if the entry is staked //                               \
        account $TOKEN_PUBKEY w                                                                                       \
        account $ENTRY_METADATA_PUBKEY w                                                                              \
        account $ENTRY_STAKE_ACCOUNT_PUBKEY w                                                                         \
        account $AUTHORITY_PUBKEY                                                                                     \
        account $CLOCK_SYSVAR_PUBKEY                                                                                  \
        account $METAPLEX_PROGRAM_PUBKEY                                                                              \
        account $STAKE_PROGRAM_PUBKEY                                                                                 \
        // The entry mint is writable because its freeze authority is passed on with the metadata authority //        \
        account $ENTRY_MINT_PUBKEY w                                                                                  \
        account $SPL_TOKEN_PROGRAM_PUBKEY                                                                             \
        // Instruction code 20 = Reauthorize //                                                                       \
        u8 20                                                                                                         \
        // Whether or not to reauthorize the Metaplex metadata //                                                     \
        bool $REAUTHORIZE_METADATA                                                                                    \
        pubkey $NEW_METADATA_AUTHORITY_PUBKEY
