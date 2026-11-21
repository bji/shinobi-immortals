#pragma once

#include "inc/types.h"
#include "util/util_accounts.c"
#include "util/util_borsh.c"
#include "util/util_calculations.c"
#include "util/util_rent.c"


// A cross-program invocation either succeeds or aborts the entire transaction with the invoked program's error;
// control never returns to this program on failure.  So functions here that do nothing but cross-program invocations
// return void, and functions that also do their own checks return only the errors of those checks.


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


// Stake accounts are 200 bytes
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


// Each entry of the stake history sysvar: the cluster-wide totals of stake as of the start of an epoch
typedef struct
{
    // The epoch that the entry is for
    uint64_t epoch;

    // The total effective stake of the cluster in that epoch
    uint64_t effective;

    // The total stake of the cluster that was activating (warming up) in that epoch
    uint64_t activating;

    // The total stake of the cluster that was deactivating (cooling down) in that epoch
    uint64_t deactivating;

} StakeHistoryEntry;


// Finds the entry for [epoch] in the stake history sysvar's data, returning true and filling in [result] if it is
// there.  The data is the bincode serialization of a vector of entries: a u64 count, then each entry as four u64
// values (epoch, effective, activating, deactivating), newest first.  [*well_formed] is set to false if the data is not
// in that format, so that the caller can tell an epoch that is not in the history from history that cannot be read.
static bool find_stake_history_entry(const SolAccountInfo *stake_history_account, uint64_t epoch,
                                     StakeHistoryEntry *result, bool *well_formed)
{
    const uint8_t *data = stake_history_account->data;
    uint32_t data_len = (uint32_t) stake_history_account->data_len;

    // The number of entries, which must all be present in the data
    uint64_t count;
    uint32_t ret = borsh_decode_u64(data, data_len, &count);
    if (!ret || (count > ((data_len - ret) / sizeof(StakeHistoryEntry)))) {
        *well_formed = false;
        return false;
    }
    data += ret, data_len -= ret;

    *well_formed = true;

    // Look through the entries for the one for the epoch.  There are at most 512 of them, so a simple search is cheap.
    for (uint64_t i = 0; i < count; i++) {
        uint64_t entry_epoch;
        borsh_decode_u64(data, data_len, &entry_epoch);
        if (entry_epoch == epoch) {
            ret = borsh_decode_u64(data, data_len, &(result->epoch));
            data += ret, data_len -= ret;
            ret = borsh_decode_u64(data, data_len, &(result->effective));
            data += ret, data_len -= ret;
            ret = borsh_decode_u64(data, data_len, &(result->activating));
            data += ret, data_len -= ret;
            borsh_decode_u64(data, data_len, &(result->deactivating));
            return true;
        }
        data += sizeof(StakeHistoryEntry), data_len -= sizeof(StakeHistoryEntry);
    }

    return false;
}


// Returns true only if it is certain that the stake program would delegate [stake], a stake account in the Stake state
// whose delegation has been deactivated, in [current_epoch], rather than refuse because it is still cooling down.  It
// is used to delegate the reference stake account again after it has been deactivated (see MeasureRewardRate), which
// must not be attempted while the stake program would refuse: the refusal would abort the whole transaction.
//
// The stake program delegates such a stake account in two cases:
//
// - In the epoch of its deactivation, it rescinds the deactivation, if the stake account is to be delegated to the
//   same vote account as before.  The reference stake account is only ever delegated to Shinobi Systems.
// - Once its effective stake has fallen to zero.  The stake program computes the effective stake of a deactivated
//   stake account from the stake history: starting from its effective stake in its deactivation epoch, each following
//   epoch takes away its share (its effective stake, over the cluster's total deactivating stake in the previous
//   epoch) of the cluster's cooldown allowance for the epoch (the cluster's effective stake in the previous epoch,
//   times the cooldown rate), and always at least 1 lamport.  For a stake account as small as the reference stake
//   account, that normally takes all of it in the first epoch after its deactivation.
//
// This repeats the stake program's calculation, but conservatively, so that it can only conclude that the stake has
// all gone when the stake program would too: its effective stake in its deactivation epoch is taken to be all of its
// delegated stake (it can be no more); the cooldown rate is taken to be 9%, the lower of the two rates that the stake
// program has used; and each epoch's reduction is computed exactly and then reduced by 1 lamport, to allow for the
// stake program's floating point calculation, whose error is far below 1 lamport for any amounts that can occur.  If
// it cannot be certain, it returns false, and the delegation is not attempted.
//
// If the stake history does not hold an entry for the deactivation epoch, the stake program counts the stake account
// as having no effective stake at all.  That happens only once the deactivation epoch is older than any epoch that the
// stake history keeps (512 epochs), which also means that the stake account has long since cooled down.
static bool is_stake_delegatable_after_deactivation(const Stake *stake, const SolAccountInfo *stake_history_account,
                                                    uint64_t current_epoch)
{
    uint64_t deactivation_epoch = stake->stake.delegation.deactivation_epoch;

    // A deactivation epoch in the future cannot happen for a stake account in the Stake state whose deactivation has
    // been requested; but if it did, nothing could be concluded
    if (current_epoch < deactivation_epoch) {
        return false;
    }

    // In the deactivation epoch itself, the stake program rescinds the deactivation
    if (current_epoch == deactivation_epoch) {
        return true;
    }

    // Otherwise, work forward from the deactivation epoch, one epoch at a time, using the stake history entry of each
    // epoch to compute the effective stake that remains in the epoch after it
    StakeHistoryEntry entry;
    bool well_formed;
    if (!find_stake_history_entry(stake_history_account, deactivation_epoch, &entry, &well_formed)) {
        // No entry for the deactivation epoch: if the stake history could be read, the stake program counts the stake
        // account as having no effective stake.  Be certain that that is because the deactivation epoch is older than
        // the stake history, rather than too recent for the stake history to have an entry for it yet.
        return well_formed && ((current_epoch - deactivation_epoch) > 512);
    }

    // The largest that the effective stake can be in the deactivation epoch
    uint64_t remaining = stake->stake.delegation.stake;

    uint64_t epoch = deactivation_epoch;
    while (true) {
        // With no deactivating stake in the cluster, the stake program takes nothing away
        if (!entry.deactivating) {
            return false;
        }

        // The exact reduction is
        //   remaining * (entry.effective * 9) / (entry.deactivating * 100)
        bool overflow = false;
        uint64_t numerator = checked_multiply(entry.effective, 9, &overflow);
        uint64_t denominator = checked_multiply(entry.deactivating, 100, &overflow);
        if (overflow) {
            return false;
        }
        // multiply_fraction requires a numerator that is no more than the denominator, so split the fraction into
        // its whole and fractional parts
        uint64_t reduction = checked_add(checked_multiply(remaining, numerator / denominator, &overflow),
                                         multiply_fraction(remaining, numerator % denominator, denominator),
                                         &overflow);
        // An overflow means a reduction far larger than the remaining stake
        if (overflow) {
            return true;
        }
        // Less 1 lamport, to allow for the stake program's floating point calculation; but the stake program always
        // takes away at least 1 lamport
        reduction = (reduction > 1) ? (reduction - 1) : 1;

        if (reduction >= remaining) {
            return true;
        }
        remaining -= reduction;

        // That was the effective stake in the epoch after [epoch].  If that is the current epoch, the stake account
        // still has effective stake.
        epoch++;
        if (epoch >= current_epoch) {
            return false;
        }

        // Go on to the next epoch.  The stake program stops, leaving stake effective, if the stake history has no
        // entry for it.
        if (!find_stake_history_entry(stake_history_account, epoch, &entry, &well_formed)) {
            return false;
        }
    }
}


typedef struct __attribute__((__packed__))
{
    uint32_t instruction; // Initialize = 0

    StakeAuthorize authorized;

    StakeLockup lockup;

} util_StakeInitializeData;


// Creates [stake_account], at the program derived address given by [seeds], funded by [funding_account_key] with its
// rent exempt reserve plus [stake_lamports], and initializes it as a stake account with the given stake and withdraw
// authorities and no lockup
static uint64_t create_stake_account(SolAccountInfo *stake_account, const SolSignerSeed *seeds, uint8_t seed_count,
                                     const SolPubkey *funding_account_key,  uint64_t stake_lamports,
                                     const SolPubkey *stake_authority_key, const SolPubkey *withdraw_authority_key,
                                     const SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    // Compute rent exempt minimum for a stake account
    uint64_t rent_exempt_minimum = get_rent_exempt_minimum(STAKE_ACCOUNT_DATA_LEN);

    // Create the pda with the desired lamports, owned by the stake program
    uint64_t ret = create_pda(stake_account, seeds, seed_count, funding_account_key, &(Constants.stake_program_pubkey),
                              rent_exempt_minimum + stake_lamports, STAKE_ACCOUNT_DATA_LEN, transaction_accounts,
                              transaction_accounts_len);
    if (ret) {
        return ret;
    }

    // Now have the stake program initialize it
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

    sol_invoke(&instruction, transaction_accounts, transaction_accounts_len);

    return 0;
}


typedef struct __attribute__((__packed__))
{
    uint32_t instruction; // Authorize = 1

    SolPubkey pubkey; // New authority

    uint32_t stake_authorize; // 0 = staker, 1 = withdrawer

} util_StakeInstructionData;


static void set_stake_authorities(const SolPubkey *stake_account, const SolPubkey *prior_withdraw_authority,
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

    sol_invoke(&instruction, transaction_accounts, transaction_accounts_len);

    data.stake_authorize = 1;

    sol_invoke(&instruction, transaction_accounts, transaction_accounts_len);
}


static void set_stake_authorities_signed(const SolPubkey *stake_account, const SolPubkey *new_authority,
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

    sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);

    data.stake_authorize = 1;

    sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
}


static void delegate_stake_signed(const SolPubkey *stake_account_key, const SolPubkey *vote_account_key,
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

    sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
}


static void deactivate_stake_signed(const SolPubkey *stake_account_key, const SolAccountInfo *transaction_accounts,
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

    sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
}
