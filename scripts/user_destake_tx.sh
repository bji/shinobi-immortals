#!/bin/bash

set -e

# Emits an encoded transaction that performs an entry destake.  The stake account withdrawal authority will be set
# to the user pubkey.
#
# Destake normally harvests the Ki that the entry has earned before it returns the stake account.  If no_ki_harvest
# is supplied, then no Ki is harvested, and any Ki that the entry had earned and not yet harvested is forfeited.  This
# exists so that the stake account can still be recovered if harvesting Ki were ever to fail; there is no other reason
# to use it.
#
# Destake also normally thaws the token account holding the entry's token, which was frozen when the entry was staked.
# If no_thaw is supplied, then the token account is left frozen; it can be thawed afterwards with user_thaw_tx.sh, and
# must be before the entry can be staked again.  This exists so that the stake account can still be recovered if
# thawing the token account were ever to fail; there is no other reason to use it.

function require ()
{
    if [ -z "$1" ]; then
        cat <<EOF

Usage: user_destake_tx.sh <USER_PUBKEY> <GROUP_NUMBER> <BLOCK_NUMBER> <ENTRY_INDEX> <STAKE_ACCOUNT_PUBKEY> \\
                         [<TOKEN_PUBKEY>] [no_ki_harvest] [no_thaw]

If <TOKEN_PUBKEY> is not provided, it is assumed to be the Associated Token Account.
If no_ki_harvest is supplied, then no Ki is harvested; unharvested Ki is forfeited.
If no_thaw is supplied, then the token account is left frozen; thaw it later with user_thaw_tx.sh.
no_ki_harvest and no_thaw may be given in either order, after <TOKEN_PUBKEY> if it is given.

EOF
        exit 1
    fi
}

USER_PUBKEY=$1
GROUP_NUMBER=$2
BLOCK_NUMBER=$3
ENTRY_INDEX=$4
STAKE_ACCOUNT_PUBKEY=$5
TOKEN_PUBKEY=

# The values of the no_ki_harvest and no_thaw flags of the instruction data; each is 1 if its argument is supplied
NO_KI_HARVEST_FLAG=0
NO_THAW_FLAG=0

# The remaining arguments are an optional <TOKEN_PUBKEY>, which must come first if given, and then the optional
# no_ki_harvest and no_thaw arguments, in either order
shift 5 2>/dev/null || true
while [ -n "$1" ]; do
    if [ "$1" = "no_ki_harvest" ] && [ $NO_KI_HARVEST_FLAG = 0 ]; then
        NO_KI_HARVEST_FLAG=1
    elif [ "$1" = "no_thaw" ] && [ $NO_THAW_FLAG = 0 ]; then
        NO_THAW_FLAG=1
    elif [ -z "$TOKEN_PUBKEY" ] && [ $NO_KI_HARVEST_FLAG = 0 ] && [ $NO_THAW_FLAG = 0 ]; then
        TOKEN_PUBKEY=$1
    else
        # Anything else forces the usage message
        require ""
    fi
    shift
done

require $USER_PUBKEY
require $GROUP_NUMBER
require $BLOCK_NUMBER
require $ENTRY_INDEX
require $STAKE_ACCOUNT_PUBKEY

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
      KI_DESTINATION_PUBKEY="pda $SPLATA_PROGRAM_PUBKEY                                                               \
                                 [ pubkey $USER_PUBKEY                                                                \
                                   pubkey $SPL_TOKEN_PROGRAM_PUBKEY                                                   \
                                   $KI_MINT_PUBKEY ]"

if [ -z "$TOKEN_PUBKEY" ]; then 
               TOKEN_PUBKEY="pda $SPLATA_PROGRAM_PUBKEY                                                               \
                                 [ pubkey $USER_PUBKEY                                                                \
                                   pubkey $SPL_TOKEN_PROGRAM_PUBKEY                                                   \
                                   $ENTRY_MINT_PUBKEY ]"
fi

solxact encode                                                                                                        \
        encoding c                                                                                                    \
        fee_payer $USER_PUBKEY                                                                                        \
        program $SELF_PROGRAM_PUBKEY                                                                                  \
        account $USER_PUBKEY ws                                                                                       \
        account $BLOCK_PUBKEY                                                                                         \
        account $ENTRY_PUBKEY w                                                                                       \
        account $USER_PUBKEY s                                                                                        \
        // The token account is writable because it is thawed when the stake account is returned, unless no_thaw //   \
        account $TOKEN_PUBKEY w                                                                                       \
        account $STAKE_ACCOUNT_PUBKEY w                                                                               \
        account $KI_DESTINATION_PUBKEY w                                                                              \
        account $USER_PUBKEY                                                                                          \
        account $KI_MINT_PUBKEY w                                                                                     \
        account $AUTHORITY_PUBKEY                                                                                     \
        account $CLOCK_SYSVAR_PUBKEY                                                                                  \
        account $SYSTEM_PROGRAM_PUBKEY                                                                                \
        account $STAKE_PROGRAM_PUBKEY                                                                                 \
        account $SPL_TOKEN_PROGRAM_PUBKEY                                                                             \
        account $SPLATA_PROGRAM_PUBKEY                                                                                \
        // The entry mint is needed to thaw the token account, unless no_thaw //                                      \
        account $ENTRY_MINT_PUBKEY                                                                                    \
        // Instruction code 14 = Destake //                                                                           \
        u8 14                                                                                                         \
        // no_ki_harvest flag: 0 = harvest Ki, 1 = do not //                                                          \
        u8 $NO_KI_HARVEST_FLAG                                                                                        \
        // no_thaw flag: 0 = thaw the token account, 1 = leave it frozen //                                           \
        u8 $NO_THAW_FLAG
