#pragma once

#include "solana_sdk.h"

#include "inc/constants.h"
#include "inc/program_config.h"
#include "util/util_rent.c"
#include "util/util_transfer_lamports.c"


static bool is_all_zeroes(const void *data, uint32_t length)
{
    const uint8_t *d = (uint8_t *) data;

    while (length--) {
        if (*d++) {
            return false;
        }
    }

    return true;
}


// This function is only called from contexts for which an "all zero pubkey" is considered to be "empty".
static bool is_empty_pubkey(const SolPubkey *pubkey)
{
    return is_all_zeroes(pubkey, sizeof(SolPubkey));
}


static bool is_superuser_pubkey(const SolPubkey *pubkey)
{
    return SolPubkey_same(&(Constants.superuser_pubkey), pubkey);
}


static bool is_nifty_config_account(const SolPubkey *pubkey)
{
    return SolPubkey_same(&(Constants.nifty_config_pubkey), pubkey);
}


static bool get_admin_account_address(const SolAccountInfo *config_account, SolPubkey *fill_in)
{
    // The identity of the admin is loaded from the config account; ensure that this is the actual one true config
    // account
    if (!is_nifty_config_account(config_account->key)) {
        return false;
    }

    // The data must be correctly sized
    if (config_account->data_len != sizeof(ProgramConfig)) {
        return false;
    }

    // Get a reference to the admin pubkey from the config
    const ProgramConfig *config = (ProgramConfig *) (config_account->data);

    *fill_in = config->admin_pubkey;

    return true;
}


static bool is_admin_account(const SolAccountInfo *config_account, const SolPubkey *pubkey)
{
    // Get a reference to the admin pubkey from the config
    SolPubkey admin_pubkey;
    if (!get_admin_account_address(config_account, &admin_pubkey)) {
        return false;
    }

    return SolPubkey_same(&admin_pubkey, pubkey);
}


static bool is_master_stake_account(const SolPubkey *pubkey)
{
    return SolPubkey_same(&(Constants.master_stake_pubkey), pubkey);
}


static bool is_ki_mint_account(const SolPubkey *pubkey)
{
    return SolPubkey_same(&(Constants.ki_mint_pubkey), pubkey);
}


static bool is_bid_marker_mint_account(const SolPubkey *pubkey)
{
    return SolPubkey_same(&(Constants.bid_marker_mint_pubkey), pubkey);
}


static bool is_shinobi_systems_vote_account(const SolPubkey *pubkey)
{
    return SolPubkey_same(&(Constants.shinobi_systems_vote_pubkey), pubkey);
}


static bool is_nifty_authority_account(const SolPubkey *pubkey)
{
    return SolPubkey_same(&(Constants.nifty_authority_pubkey), pubkey);
}


static bool is_nifty_program(const SolPubkey *pubkey)
{
    return SolPubkey_same(&(Constants.nifty_program_pubkey), pubkey);
}


static bool is_system_program(const SolPubkey *pubkey)
{
    // The system program is all zero, and thus the empty pubkey check suffices
    return is_empty_pubkey(pubkey);
}


static bool is_stake_program(const SolPubkey *pubkey)
{
    return SolPubkey_same(&(Constants.stake_program_pubkey), pubkey);
}


static bool is_spl_token_program(const SolPubkey *pubkey)
{
    return SolPubkey_same(&(Constants.spl_token_program_pubkey), pubkey);
}


static bool is_metaplex_metadata_program(const SolPubkey *pubkey)
{
    return SolPubkey_same(&(Constants.metaplex_program_pubkey), pubkey);
}


// Updates an account's data size
static void set_account_size(SolAccountInfo *account, uint64_t size)
{
    ((uint64_t *) (account->data))[-1] = size;

    account->data_len = size;
}


// To be used as data to pass to the system program when invoking CreateAccount
typedef struct __attribute__((__packed__))
{
    uint32_t instruction_code;
    uint64_t lamports;
    uint64_t space;
    SolPubkey owner;
} util_CreateAccountData;


// The target_account must be a signer
static uint64_t create_system_account(const SolPubkey *target_account_key, const SolPubkey *funding_account_key,
                                      const SolPubkey *owner_key, uint64_t space, uint64_t lamports,
                                      const SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    SolInstruction instruction;

    instruction.program_id = &(Constants.system_program_pubkey);

    SolAccountMeta account_metas[] =
          ///   0. `[WRITE, SIGNER]` Funding account
        { { /* pubkey */ (SolPubkey *) funding_account_key, /* is_writable */ true, /* is_signer */ true },
          ///   1. `[WRITE, SIGNER]` New account
          { /* pubkey */ (SolPubkey *) target_account_key, /* is_writable */ true, /* is_signer */ true } };

    instruction.accounts = account_metas;
    instruction.account_len = ARRAY_LEN(account_metas);

    util_CreateAccountData data = { 0, lamports, space, *owner_key };

    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    return sol_invoke(&instruction, transaction_accounts, transaction_accounts_len);
}


// To be used as data to pass to the system program when invoking Allocate
typedef struct __attribute__((__packed__))
{
    uint32_t instruction_code;
    uint64_t space;
} util_AllocateData;


// To be used as data to pass to the system program when invoking Assign
typedef struct __attribute__((__packed__))
{
    uint32_t instruction_code;
    SolPubkey owner;
} util_AssignData;


// Creates an account at a Program Derived Address, where the address is derived from the program id and a set of seed
// bytes.  If the account already existed, it is modified to be the correct size and owner.
static uint64_t create_pda(SolAccountInfo *new_account, const SolSignerSeed *seeds, const int seeds_count,
                           const SolPubkey *funding_account_key, const SolPubkey *owner_account_key,
                           const uint64_t funding_lamports, uint64_t space,
                           // All cross-program invocation must pass all account infos through, it's
                           // the only sane way to cross-program invoke
                           const SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    SolInstruction instruction;

    instruction.program_id = (SolPubkey *) &(Constants.system_program_pubkey);

    // The system programs's CreateAccount instruction can create it but only if its lamports are zero.  Will fail if:
    //   a. sol_invoke_signed fails because of bad seeds
    //   b. The account is not empty or is not owned by the system program (checked in allocate)
    //   c. Size to allocate is too large (checked in allocate)
    //   d. Account is not owned by the system program (checked in assign)
    //   e. Account is not writable (checked in assign)
    //   f. Account is not empty or all zeroes (checked in assign)
    if (*(new_account->lamports) == 0) {
        SolAccountMeta account_metas[] =
            // First account to pass to CreateAccount is the funding_account
            { { /* pubkey */ (SolPubkey *) funding_account_key, /* is_writable */ true, /* is_signer */ true },
              // Second account to pass to CreateAccount is the new account to be created
              { /* pubkey */ new_account->key, /* is_writable */ true, /* is_signer */ true } };

        instruction.accounts = account_metas;
        instruction.account_len = sizeof(account_metas) / sizeof(account_metas[0]);

        util_CreateAccountData data = { 0, funding_lamports, space, *owner_account_key };

        instruction.data = (uint8_t *) &data;
        instruction.data_len = sizeof(data);

        SolSignerSeeds signer_seeds = { seeds, seeds_count };

        return sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
    }

    // Else the account existed already; just ensure that it has the correct lamports, and space, and owner

    // Fund ------------------------------------------------------------------------------------------------------------
    // If the account doesn't have the required number of lamports yet, transfer the required number.  Will fail if:
    //   - The funding_lamports are not available
    if (*(new_account->lamports) < funding_lamports) {
        uint64_t ret = util_transfer_lamports(funding_account_key, new_account->key,
                                              funding_lamports - *(new_account->lamports),
                                              transaction_accounts, transaction_accounts_len);
        if (ret) {
            return ret;
        }
    }

    // Alloc -----------------------------------------------------------------------------------------------------------
    // If the account doesn't have the correct data size, "realloc" it.  Cannot fail.
    if (new_account->data_len != space) {
        set_account_size(new_account, space);
    }

    // Assign ----------------------------------------------------------------------------------------------------------

    // If already assigned to the desired owner, there is nothing to do
    if (SolPubkey_same(new_account->owner, owner_account_key)) {
        return 0;
    }

    // The owner is not the same, update it by calling the system program with the Assign instruction.  Will fail if:
    //   a. sol_invoke_signed fails because of bad seeds
    //   b. Account is not owned by system program
    //   c. Account is not writable
    //   d. Account is not empty or all zeroes
    // However, because the account exists already (has nonzero lamports), if we pass (a), then:
    //   (b) cannot fail because we know that the account is not owned by us, and it cannot be owned by any other
    //       program because that program could not have passed (a)
    //   (c) can fail but only if the caller of this function already verified writability of the account
    //   (d) can fail but only if the caller is trying to re-create an already created account, and it shouldn't do that
    // Most importantly, it's not possible for some other program to inject their own data, because they can't
    // find seeds that allow both them and us to sign for this account.
    SolAccountMeta account_metas[] =
          // Assigned account public key
        { { /* pubkey */ new_account->key, /* is_writable */ true, /* is_signer */ true } };

    instruction.accounts = account_metas;
    instruction.account_len = sizeof(account_metas) / sizeof(account_metas[0]);

    util_AssignData data = { 1, *owner_account_key };

    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    SolSignerSeeds signer_seeds = { seeds, seeds_count };

    return sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
}
