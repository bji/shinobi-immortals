#pragma once

#include "inc/types.h"
#include "util/util_borsh.c"

// These are the possible stake states
typedef enum
{
    StakeState_Uninitialized  = 0,
    StakeState_Initialized    = 1,
    StakeState_Stake          = 2,
    StakeState_RewardsPool    = 3
} StakeState;

// This is the type of the data stored in a stake account
typedef struct
{
    StakeState state;

    // If state is Initialized or Stake, meta values are set
    struct {
        uint64_t rent_exempt_reserve;
        
        struct {
            SolPubkey staker;

            SolPubkey withdrawer;
        } authorize;

        struct {
            timestamp_t unix_timestamp;

            uint64_t epoch;

            SolPubkey custodian;
        } lockup;
    } meta;

    // If stat is Stake, stake values are set
    struct {
        struct {
            SolPubkey voter_pubkey;

            uint64_t stake;

            uint64_t activation_epoch;

            uint64_t deactivation_epoch;

            double warmup_cooldown_rate;
        } delegation;

        uint64_t credits_observed;
    } stake;
} Stake;


// Returns number of bytes used, or 0 on error
static uint32_t decode_stake_account_meta(const uint8_t *data, uint32_t data_len, Stake *stake)
{
    uint32_t total = 0;
    
    uint32_t ret = borsh_decode_u64(data, data_len, &(stake->meta.rent_exempt_reserve));
    if (!ret) {
        return 0;
    }
    total += ret, data += ret, data_len -= ret;

    ret = borsh_decode_pubkey(data, data_len, &(stake->meta.authorize.staker));
    if (!ret) {
        return 0;
    }
    total += ret, data += ret, data_len -= ret;

    ret = borsh_decode_pubkey(data, data_len, &(stake->meta.authorize.withdrawer));
    if (!ret) {
        return 0;
    }
    total += ret, data += ret, data_len -= ret;

    ret = borsh_decode_u64(data, data_len, (uint64_t *) &(stake->meta.lockup.unix_timestamp));
    if (!ret) {
        return 0;
    }
    total += ret, data += ret, data_len -= ret;

    ret = borsh_decode_u64(data, data_len, &(stake->meta.lockup.epoch));
    if (!ret) {
        return 0;
    }
    total += ret, data += ret, data_len -= ret;

    ret = borsh_decode_pubkey(data, data_len, &(stake->meta.lockup.custodian));
    if (!ret) {
        return 0;
    }

    return total + ret;
}


static uint32_t decode_stake_account_stake(const uint8_t *data, uint32_t data_len, Stake *stake)
{
    uint32_t total = 0;

    uint32_t ret = borsh_decode_pubkey(data, data_len, &(stake->stake.delegation.voter_pubkey));
    if (!ret) {
        return 0;
    }
    total += ret, data += ret, data_len -= ret;

    ret = borsh_decode_u64(data, data_len, &(stake->stake.delegation.stake));
    if (!ret) {
        return 0;
    }
    total += ret, data += ret, data_len -= ret;

    ret = borsh_decode_u64(data, data_len, &(stake->stake.delegation.activation_epoch));
    if (!ret) {
        return 0;
    }
    total += ret, data += ret, data_len -= ret;

    ret = borsh_decode_u64(data, data_len, &(stake->stake.delegation.deactivation_epoch));
    if (!ret) {
        return 0;
    }
    total += ret, data += ret, data_len -= ret;

    // f64 is encoded in 8 bytes, just load it as u64 and cast it.  We don't actually ever care about
    // warmup_cooldown_rate anyway.
    ret = borsh_decode_u64(data, data_len, (uint64_t *) &(stake->stake.delegation.warmup_cooldown_rate));
    if (!ret) {
        return 0;
    }
    total += ret, data += ret, data_len -= ret;
    
    ret = borsh_decode_u64(data, data_len, (uint64_t *) &(stake->stake.credits_observed));
    if (!ret) {
        return 0;
    }

    return total + ret;
}


// Decodes the stake account into [result], returning true on success and false on failure
static bool decode_stake_account(const SolAccountInfo *stake_account, Stake *result)
{
    uint8_t *data = stake_account->data;

    uint32_t data_len = (uint32_t) stake_account->data_len;

    uint32_t stake_state;
    uint32_t ret = borsh_decode_u32(data, data_len, &stake_state);
    if (!ret) {
        return false;
    }
    data = &(data[ret]);
    data_len -= ret;

    result->state = (StakeState) stake_state;

    switch (result->state) {
    case StakeState_Uninitialized:
    case StakeState_RewardsPool:
        return true;
        
    case StakeState_Initialized:
        return (decode_stake_account_meta(data, data_len, result) != 0);

    case StakeState_Stake:
        return ((ret = decode_stake_account_meta(data, data_len, result)) &&
                decode_stake_account_stake(&(data[ret]), data_len - ret, result));

    default:
        return false;
    }
}
