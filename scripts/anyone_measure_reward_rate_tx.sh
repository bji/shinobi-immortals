#!/bin/bash

set -e

# Emits an encoded transaction that measures the reward rate that a delegator to Shinobi Systems earns, using the
# program's reference stake account, and records it in the program state for LevelUp to use.  Anyone may do this.  At
# most one measurement is made per level credit window; if the reference stake account has been deactivated, this
# delegates it to Shinobi Systems again once that is possible.  See program/anyone/anyone_measure_reward_rate.c.

function require ()
{
    if [ -z "$1" ]; then
        cat <<EOF

Usage: anyone_measure_reward_rate_tx.sh <FEE_PAYER_PUBKEY> [require_measured]

If "require_measured" is given, the transaction fails if the reward rate has not been measured in the current level
credit window and cannot be measured now.  Otherwise it succeeds, doing whatever it can (it may only take a snapshot
of the reference stake account, or do nothing at all).

EOF
        exit 1
    fi
}

FEE_PAYER_PUBKEY=$1
REQUIRE_MEASURED=$2

require $FEE_PAYER_PUBKEY

# The require_measured flag of the instruction data: 1 if asked for, 0 otherwise
case "$REQUIRE_MEASURED" in
    "")
        REQUIRE_MEASURED=0
        ;;
    require_measured)
        REQUIRE_MEASURED=1
        ;;
    *)
        require ""
        ;;
esac

# Compute program and related pubkeys

if [ -z "$SELF_PROGRAM_PUBKEY" ]; then
        SELF_PROGRAM_PUBKEY="Shin1cdrR1pmemXZU3yDV3PnQ48SX9UmrtHF4GbKzWG"
fi
if [ -z $SHINOBI_SYSTEMS_VOTE_PUBKEY ]; then
SHINOBI_SYSTEMS_VOTE_PUBKEY="BLADE1qNA1uNjRgER6DtUFf7FU3c1TWLLdpPeEcKatZ2"
fi
       STAKE_PROGRAM_PUBKEY="Stake11111111111111111111111111111111111111"
        CLOCK_SYSVAR_PUBKEY="SysvarC1ock11111111111111111111111111111111"
STAKE_HISTORY_SYSVAR_PUBKEY="SysvarStakeHistory1111111111111111111111111"
EPOCH_REWARDS_SYSVAR_PUBKEY="SysvarEpochRewards1111111111111111111111111"
        STAKE_CONFIG_PUBKEY="StakeConfig11111111111111111111111111111111"
               STATE_PUBKEY="pda $SELF_PROGRAM_PUBKEY [ u8 1 ]"
           AUTHORITY_PUBKEY="pda $SELF_PROGRAM_PUBKEY [ u8 2 ]"
     REFERENCE_STAKE_PUBKEY="pda $SELF_PROGRAM_PUBKEY [ u8 12 ]"

solxact encode                                                                                                        \
        encoding c                                                                                                    \
        fee_payer $FEE_PAYER_PUBKEY                                                                                   \
        program $SELF_PROGRAM_PUBKEY                                                                                  \
        account $STATE_PUBKEY w                                                                                       \
        account $REFERENCE_STAKE_PUBKEY w                                                                             \
        account $AUTHORITY_PUBKEY                                                                                     \
        account $CLOCK_SYSVAR_PUBKEY                                                                                  \
        account $STAKE_HISTORY_SYSVAR_PUBKEY                                                                          \
        account $EPOCH_REWARDS_SYSVAR_PUBKEY                                                                          \
        account $SHINOBI_SYSTEMS_VOTE_PUBKEY                                                                          \
        account $STAKE_CONFIG_PUBKEY                                                                                  \
        account $STAKE_PROGRAM_PUBKEY                                                                                 \
        // Instruction code 21 = MeasureRewardRate //                                                                 \
        u8 21                                                                                                         \
        // require_measured //                                                                                        \
        u8 $REQUIRE_MEASURED
