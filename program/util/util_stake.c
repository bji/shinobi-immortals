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

typedef struct
{
    SolPubkey staker;

    SolPubkey withdrawer;

} StakeAuthorize;

typedef struct
{
    timestamp_t unix_timestamp;

    uint64_t epoch;

    SolPubkey custodian;

} StakeLockup;

// This is the type of the data stored in a stake account
typedef struct
{
    StakeState state;

    // If state is Initialized or Stake, meta values are set
    struct {
        uint64_t rent_exempt_reserve;

        StakeAuthorize authorize;

        StakeLockup lockup;
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


#define STAKE_ACCOUNT_DATA_LEN 200


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


// Decodes the stake account into [result], returning true on success and false on failure.  This checks
// to make sure that the owner of the account is the Stake program and that the account is properly sized and
// formed to ensure that only a valid stake account is decoded.
static bool decode_stake_account(const SolAccountInfo *stake_account, Stake *result)
{
    // Check the stake account to ensure that it is a valid stake account for use:
    // - Must be a stake account (owned by Stake program)
    if (!is_stake_program(stake_account->owner)) {
        return false;
    }
    // - Must have proper size
    if (stake_account->data_len != STAKE_ACCOUNT_DATA_LEN) {
        return false;
    }

    const uint8_t *data = stake_account->data;

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


// Returns the minimum delegation allowed in a stake account in [fill_in].  The number of lamports that must be used
// to create a stake account must be at least this amount + the rent exempt minimum of a 200 byte account.  Returns 0
// on success, nonzero on error getting the minimum stake delegation.
static uint64_t get_minimum_stake_delegation(uint64_t *fill_in)
{
    SolInstruction instruction;

    instruction.program_id = &(Constants.stake_program_pubkey);

    instruction.accounts = 0;
    instruction.account_len = 0;

    uint32_t data = 13; // GetMinimumDelegation instruction code

    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    // Not needed, but passed just in case the runtime wants non-null values
    SolAccountInfo transaction_accounts[0];

    uint64_t ret = sol_invoke(&instruction, transaction_accounts, 0);
    if (ret) {
        return ret;
    }

    SolPubkey pubkey;
    ret = sol_get_return_data((uint8_t *) fill_in, sizeof(*fill_in), &pubkey);
    if (ret != sizeof(*fill_in)) {
        return Error_FailedToGetMinimumStakeDelegation;
    }

    return 0;
}


typedef struct __attribute__((__packed__))
{
    uint32_t instruction; // Initialize = 0

    StakeAuthorize authorized;

    StakeLockup lockup;

} util_StakeInitializeData;


static uint64_t create_stake_account(SolAccountInfo *stake_account, const SolSignerSeed *seeds, uint8_t seed_count,
                                     const SolPubkey *funding_account_key,  uint64_t stake_lamports,
                                     const SolPubkey *stake_authority_key, const SolPubkey *withdraw_authority_key,
                                     const SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    // Compute rent exempt minimum for a stake account
    uint64_t rent_exempt_minimum = get_rent_exempt_minimum(200);

    // Create the pda with the desired lamports
    {
        uint8_t prefix = PDA_Account_Seed_Prefix_Master_Stake;

        uint64_t ret = create_pda(stake_account, seeds, seed_count, funding_account_key,
                                  &(Constants.stake_program_pubkey), rent_exempt_minimum + stake_lamports, 200,
                                  transaction_accounts, transaction_accounts_len);
        if (ret) {
            return ret;
        }
    }

    SolInstruction instruction;

    instruction.program_id = &(Constants.stake_program_pubkey);

    SolAccountMeta account_metas[] =
          // `[WRITE]` Uninitialized stake account
        { { /* pubkey */ stake_account->key, /* is_writable */ true, /* is_signer */ false },
          // `[]` Rent sysvar
          { /* pubkey */ &(Constants.rent_sysvar_pubkey), /* is_writable */ false, /* is_signer */ false } };

    instruction.accounts = account_metas;
    instruction.account_len = ARRAY_LEN(account_metas);

    util_StakeInitializeData data;

    sol_memset(&data, 0, sizeof(data));

    data.authorized.staker = *stake_authority_key;
    data.authorized.withdrawer = *withdraw_authority_key;

    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    return sol_invoke(&instruction, transaction_accounts, transaction_accounts_len);
}


typedef struct __attribute__((__packed__))
{
    uint32_t instruction; // Authorize = 1

    SolPubkey pubkey; // New authority

    uint32_t stake_authorize; // 0 = staker, 1 = withdrawer

} util_StakeInstructionData;


static uint64_t set_stake_authorities(const SolPubkey *stake_account, const SolPubkey *prior_withdraw_authority,
                                      const SolPubkey *new_authority, const SolAccountInfo *transaction_accounts,
                                      int transaction_accounts_len)
{
    SolInstruction instruction;

    instruction.program_id = &(Constants.stake_program_pubkey);

    SolAccountMeta account_metas[] =
          // `[WRITE]` Stake account to be updated
        { { /* pubkey */ (SolPubkey *) stake_account, /* is_writable */ true, /* is_signer */ false },
          // `[]` Clock sysvar
          { /* pubkey */ &(Constants.clock_sysvar_pubkey), /* is_writable */ false, /* is_signer */ false },
          // `[SIGNER]` The stake or withdraw authority
          { /* pubkey */ (SolPubkey *) prior_withdraw_authority, /* is_writable */ false, /* is_signer */ true } };

    instruction.accounts = account_metas;
    instruction.account_len = ARRAY_LEN(account_metas);

    util_StakeInstructionData data = {
        /* instruction_code */ 1,
        /* pubkey */ *new_authority,
        /* stake_authorize */ 0
    };

    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    uint64_t ret = sol_invoke(&instruction, transaction_accounts, transaction_accounts_len);
    if (ret) {
        return ret;
    }

    data.stake_authorize = 1;

    return sol_invoke(&instruction, transaction_accounts, transaction_accounts_len);
}


static uint64_t set_stake_authorities_signed(const SolPubkey *stake_account, const SolPubkey *new_authority,
                                             const SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    SolInstruction instruction;

    instruction.program_id = &(Constants.stake_program_pubkey);

    SolAccountMeta account_metas[] =
          // `[WRITE]` Stake account to be updated
        { { /* pubkey */ (SolPubkey *) stake_account, /* is_writable */ true, /* is_signer */ false },
          // `[]` Clock sysvar
          { /* pubkey */ &(Constants.clock_sysvar_pubkey), /* is_writable */ false, /* is_signer */ false },
          // `[SIGNER]` The stake or withdraw authority
          { /* pubkey */ &(Constants.authority_pubkey), /* is_writable */ false, /* is_signer */ true } };

    instruction.accounts = account_metas;
    instruction.account_len = ARRAY_LEN(account_metas);

    util_StakeInstructionData data = {
        /* instruction_code */ 1,
        /* pubkey */ *new_authority,
        /* stake_authorize */ 0
    };

    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    // Now seed needs to be that of the authority
    const uint8_t *seed_bytes = (uint8_t *) Constants.authority_seed_bytes;
    SolSignerSeed seed = { seed_bytes, sizeof(Constants.authority_seed_bytes) };
    SolSignerSeeds signer_seeds = { &seed, 1 };

    uint64_t ret = sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
    if (ret) {
        return ret;
    }

    data.stake_authorize = 1;

    return sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
}


typedef struct __attribute__((__packed__))
{
    uint32_t instruction; // Split = 3

    uint64_t lamports;

} util_SplitStakeInstructionData;


typedef struct __attribute__((__packed__))
{
    uint32_t instruction; // Withdraw = 4

    uint64_t lamports;

} util_WithdrawUnstakedInstructionData;


// lamports is assumed to be at least the stake account minimum or this will fail
// moves via [bridge_account] which will be created as a PDA of the program
static uint64_t move_stake_signed(const SolPubkey *from_account_key, SolAccountInfo *bridge_account,
                                  const SolSignerSeed *bridge_seeds, int bridge_seeds_count,
                                  const SolPubkey *to_account_key, uint64_t lamports,
                                  const SolPubkey *funding_account_key,
                                  const SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    // Compute rent exempt minimum for a stake account
    uint64_t rent_exempt_minimum = get_rent_exempt_minimum(200);

    // Create the bridge account as a PDA to ensure that it exist with proper ownership
    uint64_t ret = create_pda(bridge_account, bridge_seeds, bridge_seeds_count, funding_account_key,
                              &(Constants.stake_program_pubkey), rent_exempt_minimum, 200, transaction_accounts,
                              transaction_accounts_len);
    if (ret) {
        return ret;
    }

    SolInstruction instruction;
    instruction.program_id = &(Constants.stake_program_pubkey);

    // Now seed needs to be that of the authority
    const uint8_t *seed_bytes = (uint8_t *) Constants.authority_seed_bytes;
    SolSignerSeed seed = { seed_bytes, sizeof(Constants.authority_seed_bytes) };
    SolSignerSeeds signer_seeds = { &seed, 1 };

    // Split [lamports] from [from_account] into [bridge_account]
    {
        SolAccountMeta account_metas[] =
              // `[WRITE]` Stake account to be split; must be in the Initialized or Stake state
            { { /* pubkey */ (SolPubkey *) from_account_key, /* is_writable */ true, /* is_signer */ false },
              // `[WRITE]` Uninitialized stake account that will take the split-off amount
              { /* pubkey */ (SolPubkey *) bridge_account->key, /* is_writable */ true, /* is_signer */ false },
              // `[SIGNER]` Stake authority
              { /* pubkey */ &(Constants.authority_pubkey), /* is_writable */ false, /* is_signer */ true } };

        instruction.accounts = account_metas;
        instruction.account_len = ARRAY_LEN(account_metas);

        util_SplitStakeInstructionData data = {
            /* instruction_code */ 3,
            /* lamports */ lamports
        };

        instruction.data = (uint8_t *) &data;
        instruction.data_len = sizeof(data);

        ret = sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
        if (ret) {
            return ret;
        }
    }

    // Merge [bridge_account] into [to_account]
    {
        SolAccountMeta account_metas[] =
              // `[WRITE]` Destination stake account for the merge
            { { /* pubkey */ (SolPubkey *) to_account_key, /* is_writable */ true, /* is_signer */ false },
              // `[WRITE]` Source stake account for to merge.  This account will be drained
              { /* pubkey */ (SolPubkey *) bridge_account->key, /* is_writable */ true, /* is_signer */ false },
              // `[]` Clock sysvar
              { /* pubkey */ &(Constants.clock_sysvar_pubkey), /* is_writable */ false, /* is_signer */ false },
              // `[]` Stake history sysvar that carries stake warmup/cooldown history
              { /* pubkey */ &(Constants.stake_history_sysvar_pubkey), /* is_writable */ false, /* is_signer */ false },
              // `[SIGNER]` Stake authority
              { /* pubkey */ &(Constants.authority_pubkey), /* is_writable */ false, /* is_signer */ true } };

        instruction.accounts = account_metas;
        instruction.account_len = ARRAY_LEN(account_metas);

        uint32_t data = 7; // Merge

        instruction.data = (uint8_t *) &data;
        instruction.data_len = sizeof(data);

        ret = sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
        if (ret) {
            return ret;
        }
    }

    // Finally, withdraw the rent exempt minimum back to the funding account
    {
        SolAccountMeta account_metas[] =
              ///   0. `[WRITE]` Stake account from which to withdraw
            { { /* pubkey */ (SolPubkey *) to_account_key, /* is_writable */ true, /* is_signer */ false },
              ///   1. `[WRITE]` Recipient account
              { /* pubkey */ (SolPubkey *) funding_account_key, /* is_writable */ true, /* is_signer */ false },
              ///   2. `[]` Clock sysvar
              { /* pubkey */ &(Constants.clock_sysvar_pubkey), /* is_writable */ false, /* is_signer */ false },
              ///   3. `[]` Stake history sysvar that carries stake warmup/cooldown history
              { /* pubkey */ &(Constants.stake_history_sysvar_pubkey), /* is_writable */ false, /* is_signer */ false },
              ///   4. `[SIGNER]` Withdraw authority
              { /* pubkey */ &(Constants.authority_pubkey), /* is_writable */ false, /* is_signer */ true } };

        instruction.accounts = account_metas;
        instruction.account_len = ARRAY_LEN(account_metas);

        util_WithdrawUnstakedInstructionData data = {
            /* instruction_code */ 4,
            /* lamports */ rent_exempt_minimum
        };

        instruction.data = (uint8_t *) &data;
        instruction.data_len = sizeof(data);

        return sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
    }
}


static uint64_t split_master_stake_signed(const SolPubkey *to_account_key, uint64_t lamports,
                                          const SolPubkey *funding_account_key,
                                          const SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    // Create the to_account using the system program
    uint64_t ret = create_system_account(to_account_key, funding_account_key, &(Constants.stake_program_pubkey), 200,
                                         get_rent_exempt_minimum(200), transaction_accounts, transaction_accounts_len);
    if (ret) {
        return ret;
    }

    // Now use the stake program to split

    SolInstruction instruction;

    instruction.program_id = &(Constants.stake_program_pubkey);

    SolAccountMeta account_metas[] =
          // `[WRITE]` Stake account to be split; must be in the Initialized or Stake state
        { { /* pubkey */ &(Constants.master_stake_pubkey), /* is_writable */ true, /* is_signer */ false },
          // `[WRITE]` Uninitialized stake account that will take the split-off amount
          { /* pubkey */ (SolPubkey *) to_account_key, /* is_writable */ true, /* is_signer */ false },
          // `[SIGNER]` Stake authority
          { /* pubkey */ &(Constants.authority_pubkey), /* is_writable */ false, /* is_signer */ true } };

    instruction.accounts = account_metas;
    instruction.account_len = ARRAY_LEN(account_metas);

    util_SplitStakeInstructionData data = {
        /* instruction_code */ 3,
        /* lamports */ lamports
    };

    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    // Seed needs to be that of the authority
    const uint8_t *seed_bytes = (uint8_t *) Constants.authority_seed_bytes;
    SolSignerSeed seed = { seed_bytes, sizeof(Constants.authority_seed_bytes) };
    SolSignerSeeds signer_seeds = { &seed, 1 };

    ret = sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
    if (ret) {
        return ret;
    }

    // Now re-assign authorities of the new account to the funding account
    return set_stake_authorities_signed(to_account_key, funding_account_key, transaction_accounts,
                                        transaction_accounts_len);
}


static uint64_t delegate_stake_signed(const SolPubkey *stake_account_key, const SolPubkey *vote_account_key,
                                      const SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    SolInstruction instruction;

    instruction.program_id = &(Constants.stake_program_pubkey);

    SolAccountMeta account_metas[] =
          // `[WRITE]` Initialized stake account to be delegated
        { { /* pubkey */ (SolPubkey *) stake_account_key, /* is_writable */ true, /* is_signer */ false },
          // `[]` Vote account to which this stake will be delegated
          { /* pubkey */ (SolPubkey *) vote_account_key, /* is_writable */ false, /* is_signer */ false },
          // `[]` Clock sysvar
          { /* pubkey */ &(Constants.clock_sysvar_pubkey), /* is_writable */ false, /* is_signer */ false },
          // `[]` Stake history sysvar that carries stake warmup/cooldown history
          { /* pubkey */ &(Constants.stake_history_sysvar_pubkey), /* is_writable */ false, /* is_signer */ false },
          // `[]` Address of config account that carries stake config
          { /* pubkey */ &(Constants.stake_config_pubkey), /* is_writable */ false, /* is_signer */ false },
          // `[SIGNER]` Stake authority
          { /* pubkey */ &(Constants.authority_pubkey), /* is_writable */ false, /* is_signer */ true } };

    instruction.accounts = account_metas;
    instruction.account_len = ARRAY_LEN(account_metas);

    uint32_t data = 2; // DelegateStake

    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    // Now seed needs to be that of the authority
    const uint8_t *seed_bytes = (uint8_t *) Constants.authority_seed_bytes;
    SolSignerSeed seed = { seed_bytes, sizeof(Constants.authority_seed_bytes) };
    SolSignerSeeds signer_seeds = { &seed, 1 };

    return sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
}


static uint64_t deactivate_stake_signed(const SolPubkey *stake_account_key, const SolAccountInfo *transaction_accounts,
                                        int transaction_accounts_len)
{
    SolInstruction instruction;

    instruction.program_id = &(Constants.stake_program_pubkey);

    SolAccountMeta account_metas[] =
          // `[WRITE]` Delegated stake account
        { { /* pubkey */ (SolPubkey *) stake_account_key, /* is_writable */ true, /* is_signer */ false },
          // `[]` Clock sysvar
          { /* pubkey */ &(Constants.clock_sysvar_pubkey), /* is_writable */ false, /* is_signer */ false },
          // `[SIGNER]` Stake authority
          { /* pubkey */ &(Constants.authority_pubkey), /* is_writable */ false, /* is_signer */ true } };

    instruction.accounts = account_metas;
    instruction.account_len = ARRAY_LEN(account_metas);

    uint32_t data = 5; // Deactivate

    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    // Now seed needs to be that of the authority
    const uint8_t *seed_bytes = (uint8_t *) Constants.authority_seed_bytes;
    SolSignerSeed seed = { seed_bytes, sizeof(Constants.authority_seed_bytes) };
    SolSignerSeeds signer_seeds = { &seed, 1 };

    return sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
}
