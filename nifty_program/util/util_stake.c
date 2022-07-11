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


typedef struct
{
    uint32_t instruction; // Initialize = 0

    StakeAuthorize authorized;

    StakeLockup lockup;
    
} util_StakeInitializeData;


static bool create_stake_account(SolAccountInfo *stake_account, SolSignerSeed *seeds, uint8_t seed_count,
                                 SolPubkey *funding_account_key,  uint64_t stake_lamports,
                                 SolPubkey *stake_authority_key, SolPubkey *withdraw_authority_key,
                                 SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    // Compute rent exempt minimum for a stake account
    uint64_t rent_exempt_minimum = get_rent_exempt_minimum(200);
    
    // Create the pda with the desired lamports
    {
        uint8_t prefix = PDA_Account_Seed_Prefix_Master_Stake;

        if (create_pda(stake_account, seeds, seed_count, funding_account_key,
                       &(Constants.stake_program_pubkey), rent_exempt_minimum + stake_lamports, 200,
                       transaction_accounts, transaction_accounts_len)) {
            return false;
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
    instruction.account_len = sizeof(account_metas) / sizeof(account_metas[0]);

    util_StakeInitializeData data;
    
    sol_memset(&data, 0, sizeof(data));
    
    data.authorized.staker = *stake_authority_key;
    data.authorized.withdrawer = *withdraw_authority_key;
    
    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    return (sol_invoke(&instruction, transaction_accounts, transaction_accounts_len) == 0);
}


typedef struct
{
    uint32_t instruction; // Authorize = 1

    SolPubkey pubkey; // New authority

    uint32_t stake_authorize; // 0 = staker, 1 = withdrawer
    
} util_StakeInstructionData;


static bool set_stake_authorities(SolPubkey *stake_account, SolPubkey *prior_withdraw_authority,
                                  SolPubkey *new_authority, SolAccountInfo *transaction_accounts,
                                  int transaction_accounts_len)
{
    SolInstruction instruction;

    instruction.program_id = &(Constants.stake_program_pubkey);
    
    SolAccountMeta account_metas[] = 
          // `[WRITE]` Stake account to be updated
        { { /* pubkey */ stake_account, /* is_writable */ true, /* is_signer */ false },
          // `[]` Clock sysvar
          { /* pubkey */ &(Constants.clock_sysvar_pubkey), /* is_writable */ false, /* is_signer */ false },
          // `[SIGNER]` The stake or withdraw authority
          { /* pubkey */ prior_withdraw_authority, /* is_writable */ false, /* is_signer */ true } };
    
    instruction.accounts = account_metas;
    instruction.account_len = sizeof(account_metas) / sizeof(account_metas[0]);
    
    util_StakeInstructionData data = {
        /* instruction_code */ 1,
        /* pubkey */ *new_authority,
        /* stake_authorize */ 0
    };
    
    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    if (sol_invoke(&instruction, transaction_accounts, transaction_accounts_len)) {
        return false;
    }

    data.stake_authorize = 1;
    
    return (sol_invoke(&instruction, transaction_accounts, transaction_accounts_len) == 0);
}


static bool set_stake_authorities_signed(SolPubkey *stake_account, SolPubkey *prior_withdraw_authority,
                                         SolPubkey *new_authority, SolAccountInfo *transaction_accounts,
                                         int transaction_accounts_len)
{
    SolInstruction instruction;

    instruction.program_id = &(Constants.stake_program_pubkey);
    
    SolAccountMeta account_metas[] = 
          // `[WRITE]` Stake account to be updated
        { { /* pubkey */ stake_account, /* is_writable */ true, /* is_signer */ false },
          // `[]` Clock sysvar
          { /* pubkey */ &(Constants.clock_sysvar_pubkey), /* is_writable */ false, /* is_signer */ false },
          // `[SIGNER]` The stake or withdraw authority
          { /* pubkey */ prior_withdraw_authority, /* is_writable */ false, /* is_signer */ true } };
    
    instruction.accounts = account_metas;
    instruction.account_len = sizeof(account_metas) / sizeof(account_metas[0]);
    
    util_StakeInstructionData data = {
        /* instruction_code */ 1,
        /* pubkey */ *new_authority,
        /* stake_authorize */ 0
    };
    
    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    // Now seed needs to be that of the nifty authority
    uint8_t *seed_bytes = (uint8_t *) Constants.nifty_authority_seed_bytes;
    SolSignerSeed seed = { seed_bytes, sizeof(Constants.nifty_authority_seed_bytes) };
    SolSignerSeeds signer_seeds = { &seed, 1 };
    
    if (sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1)) {
        return false;
    }

    data.stake_authorize = 1;
    
    return (sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1) == 0);
}


typedef struct
{
    uint32_t instruction; // Authorize = 1

    uint64_t lamports;

} util_SplitStakeInstructionData;


// lamports is assumed to be at least the stake account minimum or this will fail
// moves via [bridge_account] which will be created as a PDA of the nifty_program
static bool move_stake_signed(SolPubkey *from_account_key,  SolAccountInfo *bridge_account,
                              uint8_t bridge_bump_seed, SolPubkey *to_account_key, uint64_t lamports,
                              SolPubkey *funding_account_key, SolAccountInfo *transaction_accounts,
                              int transaction_accounts_len)
{
    // Compute rent exempt minimum for a stake account
    // XXX TRY CREATING THE BRIDGE ACCOUNT WITH 0 LAMPORTS TO ALLOW ALL OF IT TO COME FROM [from_account]
    // If this doesn't work, then have to transfer rent_exempt_minimum from funding_account.
    uint64_t rent_exempt_minimum = /* get_rent_exempt_minimum(200) */ 0;
    
    // Create the bridge account as a PDA to ensure that it exist with proper ownership
    {
        uint8_t prefix = PDA_Account_Seed_Prefix_Bridge;
        SolSignerSeed seed_parts[] = { { &prefix, 1 },
                                       { (uint8_t *) from_account_key, sizeof(SolPubkey) },
                                       { &bridge_bump_seed, 1 } };
        if (create_pda(bridge_account, seed_parts, sizeof(seed_parts) / sizeof(seed_parts[0]), funding_account_key,
                       &(Constants.stake_program_pubkey), rent_exempt_minimum, 200, transaction_accounts,
                       transaction_accounts_len)) {
            return false;
        }
    }

    SolInstruction instruction;
    instruction.program_id = &(Constants.stake_program_pubkey);
    
    // Now seed needs to be that of the nifty authority
    uint8_t *seed_bytes = (uint8_t *) Constants.nifty_authority_seed_bytes;
    SolSignerSeed seed = { seed_bytes, sizeof(Constants.nifty_authority_seed_bytes) };
    SolSignerSeeds signer_seeds = { &seed, 1 };

    // Split [lamports] from [from_account] into [bridge_account]
    {
        SolAccountMeta account_metas[] = 
        // `[WRITE]` Stake account to be split; must be in the Initialized or Stake state
            { { /* pubkey */ from_account_key, /* is_writable */ true, /* is_signer */ false },
              // `[WRITE]` Uninitialized stake account that will take the split-off amount
              { /* pubkey */ bridge_account->key, /* is_writable */ true, /* is_signer */ false },
              // `[SIGNER]` Stake authority
              { /* pubkey */ &(Constants.nifty_authority_pubkey), /* is_writable */ false, /* is_signer */ true } };
        
        util_SplitStakeInstructionData data = {
            /* instruction_code */ 3,
            /* lamports */ lamports
        };
        
        instruction.accounts = account_metas;
        instruction.account_len = sizeof(account_metas) / sizeof(account_metas[0]);
        instruction.data = (uint8_t *) &data;
        instruction.data_len = sizeof(data);
        
        if (sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1)) {
            return false;
        }
    }

    // Merge [bridge_account] into [to_account]
    {
        SolAccountMeta account_metas[] = 
              // `[WRITE]` Destination stake account for the merge
            { { /* pubkey */ to_account_key, /* is_writable */ true, /* is_signer */ false },
              // `[WRITE]` Source stake account for to merge.  This account will be drained
              { /* pubkey */ bridge_account->key, /* is_writable */ true, /* is_signer */ false },
              // `[]` Clock sysvar
              { /* pubkey */ &(Constants.clock_sysvar_pubkey), /* is_writable */ false, /* is_signer */ false },
              // `[]` Stake history sysvar that carries stake warmup/cooldown history
              { /* pubkey */ &(Constants.stake_history_sysvar_pubkey), /* is_writable */ false, /* is_signer */ false },
              // `[SIGNER]` Stake authority
              { /* pubkey */ &(Constants.nifty_authority_pubkey), /* is_writable */ false, /* is_signer */ true } };

        uint32_t data = 7; // Merge
        
        instruction.accounts = account_metas;
        instruction.account_len = sizeof(account_metas) / sizeof(account_metas[0]);
        instruction.data = (uint8_t *) &data;
        instruction.data_len = sizeof(data);
        
        return (sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1) == 0);
    }
}


static bool delegate_stake_signed(SolPubkey *stake_account_key, SolPubkey *vote_account_key,
                                  SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    SolInstruction instruction;
    
    instruction.program_id = &(Constants.stake_program_pubkey);
    
    SolAccountMeta account_metas[] = 
          // `[WRITE]` Initialized stake account to be delegated
        { { /* pubkey */ stake_account_key, /* is_writable */ true, /* is_signer */ false },
          // `[]` Vote account to which this stake will be delegated
          { /* pubkey */ vote_account_key, /* is_writable */ false, /* is_signer */ false },
          // `[]` Clock sysvar
          { /* pubkey */ &(Constants.clock_sysvar_pubkey), /* is_writable */ false, /* is_signer */ false },
          // `[]` Stake history sysvar that carries stake warmup/cooldown history
          { /* pubkey */ &(Constants.stake_history_sysvar_pubkey), /* is_writable */ false, /* is_signer */ false },
          // `[]` Address of config account that carries stake config
          { /* pubkey */ &(Constants.stake_config_pubkey), /* is_writable */ false, /* is_signer */ false },
          // `[SIGNER]` Stake authority
          { /* pubkey */ &(Constants.nifty_authority_pubkey), /* is_writable */ false, /* is_signer */ true } };

    uint32_t data = 2; // DelegateStake
        
    instruction.accounts = account_metas;
    instruction.account_len = sizeof(account_metas) / sizeof(account_metas[0]);
    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);
    
    // Now seed needs to be that of the nifty authority
    uint8_t *seed_bytes = (uint8_t *) Constants.nifty_authority_seed_bytes;
    SolSignerSeed seed = { seed_bytes, sizeof(Constants.nifty_authority_seed_bytes) };
    SolSignerSeeds signer_seeds = { &seed, 1 };

    return (sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1) == 0);
}


static bool deactivate_stake_signed(SolPubkey *stake_account_key, SolAccountInfo *transaction_accounts,
                                    int transaction_accounts_len)
{
    SolInstruction instruction;
    
    instruction.program_id = &(Constants.stake_program_pubkey);
    
    SolAccountMeta account_metas[] = 
          // `[WRITE]` Delegated stake account
        { { /* pubkey */ stake_account_key, /* is_writable */ true, /* is_signer */ false },
          // `[]` Clock sysvar
          { /* pubkey */ &(Constants.clock_sysvar_pubkey), /* is_writable */ false, /* is_signer */ false },
          // `[SIGNER]` Stake authority
          { /* pubkey */ &(Constants.nifty_authority_pubkey), /* is_writable */ false, /* is_signer */ true } };

    uint32_t data = 5; // Deactivate
        
    instruction.accounts = account_metas;
    instruction.account_len = sizeof(account_metas) / sizeof(account_metas[0]);
    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);
    
    // Now seed needs to be that of the nifty authority
    uint8_t *seed_bytes = (uint8_t *) Constants.nifty_authority_seed_bytes;
    SolSignerSeed seed = { seed_bytes, sizeof(Constants.nifty_authority_seed_bytes) };
    SolSignerSeeds signer_seeds = { &seed, 1 };

    return (sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1) == 0);
}
