#!/bin/bash

set -e

# Emits an encoded transaction that settles the winning bid of an auction that has ended: the entry's token goes to
# the winning bidder's associated token account, the winning bid goes to the admin, and anything that the bid account
# holds beyond the winning bid goes back to the bidder.
#
# The only signature that this transaction needs is that of the funding account, which is the fee payer and also
# pays for the creation of the bidder's associated token account if it does not exist yet.
#
# The winning bidder may settle their own bid at any time after the auction has ended, by giving their own pubkey as
# both the funding account and the bidder.  Anyone else may settle it, with their own pubkey as the funding account,
# but only once 30 days have passed since the end of the auction.
#
# When the bidder settles their own bid, the transaction also burns the bidder's bid marker tokens for the entry and
# closes the bid marker token account, returning its lamports to the bidder, since the bid marker has no purpose once
# the bid is settled.  Supply keep_bid_marker as the last argument to leave the bid marker alone, which is necessary
# if the bidder has already burned or closed it.  When anyone else settles, the bid marker is always left alone,
# because reclaiming it needs the bidder's signature.

function require ()
{
    if [ -z "$1" ]; then
        cat <<EOF

Usage: anyone_settle_winning_bid_tx.sh <FUNDING_PUBKEY> <ADMIN_PUBKEY> <BIDDER_PUBKEY> \\
                                       <GROUP_NUMBER> <BLOCK_NUMBER> <ENTRY_INDEX> [keep_bid_marker]

EOF
        exit 1
    fi
}

FUNDING_PUBKEY=$1
ADMIN_PUBKEY=$2
BIDDER_PUBKEY=$3
GROUP_NUMBER=$4
BLOCK_NUMBER=$5
ENTRY_INDEX=$6
KEEP_BID_MARKER=$7

require $FUNDING_PUBKEY
require $ADMIN_PUBKEY
require $BIDDER_PUBKEY
require $GROUP_NUMBER
require $BLOCK_NUMBER
require $ENTRY_INDEX

# The pubkeys computed below that belong to a user (the token destination, the bid marker token account and the bid
# account) are those of the bidder
USER_PUBKEY=$BIDDER_PUBKEY

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
             KI_MINT_PUBKEY="pda $SELF_PROGRAM_PUBKEY [ u8 3 ]"
     BID_MARKER_MINT_PUBKEY="pda $SELF_PROGRAM_PUBKEY [ u8 7 ]"
         KI_METADATA_PUBKEY="pda $METAPLEX_PROGRAM_PUBKEY                                                             \
                                 [ string metadata                                                                    \
                                   pubkey $METAPLEX_PROGRAM_PUBKEY                                                    \
                                   $KI_MINT_PUBKEY ]"
 BID_MARKER_METADATA_PUBKEY="pda $METAPLEX_PROGRAM_PUBKEY                                                             \
                                 [ string metadata                                                                    \
                                   pubkey $METAPLEX_PROGRAM_PUBKEY                                                    \
                                   $BID_MARKER_MINT_PUBKEY ]"
               BLOCK_PUBKEY="pda $SELF_PROGRAM_PUBKEY                                                                 \
                                 [ u8 10                                                                              \
                                   u32 $GROUP_NUMBER                                                                  \
                                   u32 $BLOCK_NUMBER ]"
           WHITELIST_PUBKEY="pda $SELF_PROGRAM_PUBKEY                                                                 \
                                 [ u8 9                                                                               \
                                   $BLOCK_PUBKEY ]"
          ENTRY_MINT_PUBKEY="pda $SELF_PROGRAM_PUBKEY                                                                 \
                                 [ u8 4                                                                               \
                                   $BLOCK_PUBKEY                                                                      \
                                   u16 $ENTRY_INDEX ]"
               ENTRY_PUBKEY="pda $SELF_PROGRAM_PUBKEY                                                                 \
                                 [ u8 11                                                                              \
                                   $ENTRY_MINT_PUBKEY ]"
         ENTRY_TOKEN_PUBKEY="pda $SELF_PROGRAM_PUBKEY                                                                 \
                                 [ u8 5                                                                               \
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
                                 [ u8 8                                                                               \
                                   $ENTRY_MINT_PUBKEY                                                                 \
                                   pubkey $USER_PUBKEY ]"
                 BID_PUBKEY="pda $SELF_PROGRAM_PUBKEY                                                                 \
                                 [ u8 6                                                                               \
                                   $BID_MARKER_TOKEN_PUBKEY ]"

# The bid marker is reclaimed only if the bidder is the one settling, and has not asked to keep it.  Reclaiming it
# takes two more accounts at the end of the account list.
if [ "$FUNDING_PUBKEY" = "$BIDDER_PUBKEY" -a "$KEEP_BID_MARKER" != "keep_bid_marker" ]; then
    EXTRA_ACCOUNTS="account $BID_MARKER_MINT_PUBKEY w                                                                 \
                    account $BID_MARKER_TOKEN_PUBKEY w"
else
    EXTRA_ACCOUNTS=
fi

solxact encode                                                                                                        \
        encoding c                                                                                                    \
        fee_payer $FUNDING_PUBKEY                                                                                     \
        program $SELF_PROGRAM_PUBKEY                                                                                  \
        account $FUNDING_PUBKEY ws                                                                                    \
        account $ENTRY_PUBKEY w                                                                                       \
        account $BID_PUBKEY w                                                                                         \
        account $STATE_PUBKEY                                                                                         \
        account $ADMIN_PUBKEY w                                                                                       \
        account $ENTRY_TOKEN_PUBKEY w                                                                                 \
        account $ENTRY_MINT_PUBKEY                                                                                    \
        account $AUTHORITY_PUBKEY                                                                                     \
        // The bidder's associated token account for the entry's mint, which receives the token //                    \
        account $TOKEN_DESTINATION_PUBKEY w                                                                           \
        // The bidder is writable because it receives any lamports of the bid account beyond the bid //               \
        account $BIDDER_PUBKEY w                                                                                      \
        account $SYSTEM_PROGRAM_PUBKEY                                                                                \
        account $SPL_TOKEN_PROGRAM_PUBKEY                                                                             \
        account $SPLATA_PROGRAM_PUBKEY                                                                                \
        account $ENTRY_METADATA_PUBKEY w                                                                              \
        account $METAPLEX_PROGRAM_PUBKEY                                                                              \
        $EXTRA_ACCOUNTS                                                                                               \
        // Instruction code 18 = SettleWinningBid //                                                                  \
        u8 18
