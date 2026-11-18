#!/bin/bash

set -e

# Emits an encoded transaction that withdraws to the admin the excess lamports of an account that the program
# controls: everything that the account holds beyond what it is required to hold.  The amount is computed by the
# program; there is nothing to specify.  Must be signed by the admin, who is also the fee payer and the recipient of
# the lamports.
#
# The account is named by its type, plus the block and entry numbers for those types that need them:
#
#   master_stake                         The master stake account.  Its lamports that are not staked are withdrawn,
#                                        except that 2 SOL is always left in addition to its rent exempt reserve.
#   state                                The program state account
#   authority                            The authority account.  The purchase prices of mysteries that have not yet
#                                        been revealed or refunded are held there in escrow, and are never withdrawn.
#   block <GROUP_NUMBER> <BLOCK_NUMBER>
#   entry <GROUP_NUMBER> <BLOCK_NUMBER> <ENTRY_INDEX>
#   whitelist <GROUP_NUMBER> <BLOCK_NUMBER>

function usage ()
{
    cat <<EOF

Usage: admin_withdraw_excess_tx.sh <ADMIN_PUBKEY> master_stake
       admin_withdraw_excess_tx.sh <ADMIN_PUBKEY> state
       admin_withdraw_excess_tx.sh <ADMIN_PUBKEY> authority
       admin_withdraw_excess_tx.sh <ADMIN_PUBKEY> block <GROUP_NUMBER> <BLOCK_NUMBER>
       admin_withdraw_excess_tx.sh <ADMIN_PUBKEY> entry <GROUP_NUMBER> <BLOCK_NUMBER> <ENTRY_INDEX>
       admin_withdraw_excess_tx.sh <ADMIN_PUBKEY> whitelist <GROUP_NUMBER> <BLOCK_NUMBER>

EOF
    exit 1
}

function require ()
{
    if [ -z "$1" ]; then
        usage
    fi
}

ADMIN_PUBKEY=$1
ACCOUNT_TYPE=$2
GROUP_NUMBER=$3
BLOCK_NUMBER=$4
ENTRY_INDEX=$5

require $ADMIN_PUBKEY
require $ACCOUNT_TYPE

# Check that the block and entry numbers needed to find an account of the given type were supplied
case "$ACCOUNT_TYPE" in
    master_stake|state|authority)
        ;;
    block|whitelist)
        require $GROUP_NUMBER
        require $BLOCK_NUMBER
        ;;
    entry)
        require $GROUP_NUMBER
        require $BLOCK_NUMBER
        require $ENTRY_INDEX
        ;;
    *)
        usage
        ;;
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

# Choose the account to withdraw from, and the account type code that the program knows it by
# (WithdrawExcessAccountType in program/admin/admin_withdraw_excess.c)
case "$ACCOUNT_TYPE" in
    master_stake)
        FROM_PUBKEY=$MASTER_STAKE_PUBKEY
        ACCOUNT_TYPE_CODE=0
        ;;
    state)
        FROM_PUBKEY=$STATE_PUBKEY
        ACCOUNT_TYPE_CODE=1
        ;;
    authority)
        FROM_PUBKEY=$AUTHORITY_PUBKEY
        ACCOUNT_TYPE_CODE=2
        ;;
    block)
        FROM_PUBKEY=$BLOCK_PUBKEY
        ACCOUNT_TYPE_CODE=3
        ;;
    entry)
        FROM_PUBKEY=$ENTRY_PUBKEY
        ACCOUNT_TYPE_CODE=4
        ;;
    whitelist)
        FROM_PUBKEY=$WHITELIST_PUBKEY
        ACCOUNT_TYPE_CODE=5
        ;;
esac

# The authority account, clock sysvar, stake program and stake history sysvar are only used by the program when
# withdrawing from the master stake account, but the instruction requires them for every account type
solxact encode                                                                                                        \
        encoding c                                                                                                    \
        fee_payer $ADMIN_PUBKEY                                                                                       \
        program $SELF_PROGRAM_PUBKEY                                                                                  \
        account $STATE_PUBKEY                                                                                         \
        account $ADMIN_PUBKEY ws                                                                                      \
        account $FROM_PUBKEY w                                                                                        \
        account $AUTHORITY_PUBKEY                                                                                     \
        account $CLOCK_SYSVAR_PUBKEY                                                                                  \
        account $STAKE_PROGRAM_PUBKEY                                                                                 \
        account $STAKE_HISTORY_SYSVAR_PUBKEY                                                                          \
        // Instruction code 23 = WithdrawExcess //                                                                    \
        u8 23                                                                                                         \
        u8 $ACCOUNT_TYPE_CODE
