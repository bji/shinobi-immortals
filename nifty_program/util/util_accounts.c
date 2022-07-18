#pragma once

#include "solana_sdk.h"

#include "inc/constants.h"
#include "inc/program_config.h"
#include "util/util_rent.c"
#include "util/util_transfer_lamports.c"


static bool is_all_zeroes(const void *data, uint32_t length)
{
    uint8_t *d = (uint8_t *) data;
    
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

    // The data must be correctly sized -- may be larger than, but never smaller than, the expected size
    if (config_account->data_len < sizeof(ProgramConfig)) {
        return false;
    }

    // Get a reference to the admin pubkey from the config
    ProgramConfig *config = (ProgramConfig *) (config_account->data);

    *fill_in = config->admin_pubkey;

    return true;
}


static bool is_admin_account(const SolAccountInfo *config_account, SolPubkey *pubkey)
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


static bool is_metaplex_metadata_program(SolPubkey *pubkey)
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
static uint64_t create_system_account(SolPubkey *target_account_key, SolPubkey *funding_account_key,
                                      SolPubkey *owner_key, uint64_t space, uint64_t lamports,
                                      SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    SolInstruction instruction;
    
    instruction.program_id = &(Constants.system_program_pubkey);

    SolAccountMeta account_metas[] = 
          ///   0. `[WRITE, SIGNER]` Funding account
        { { /* pubkey */ funding_account_key, /* is_writable */ true, /* is_signer */ true },
          ///   1. `[WRITE, SIGNER]` New account
          { /* pubkey */ target_account_key, /* is_writable */ true, /* is_signer */ true } };

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
// bytes.
// A PDA account can be created only if:
// - It doesn't already exist
// - It exists already but was owned by the System program
// - It exists already but was owned by this program
// In any other case, the account cannot be re-assigned and thus cannot be "created" as a PDA.  This should not be
// a problem because every PDA that was assigned to some other entity is either never deleted (e.g. token mint
// account), or its function should be known and the closure of the account should be allowed through an instruction
// issued directly to that program (i.e. close token account, close stake account, etc).
static uint64_t create_pda(SolAccountInfo *new_account, SolSignerSeed *seeds, int seeds_count,
                           SolPubkey *funding_account_key, SolPubkey *owner_account_key,
                           uint64_t funding_lamports, uint64_t space,
                           // All cross-program invocation must pass all account infos through, it's
                           // the only sane way to cross-program invoke
                           SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    SolInstruction instruction;
    
    instruction.program_id = (SolPubkey *) &(Constants.system_program_pubkey);

    SolSignerSeeds signer_seeds = { seeds, seeds_count };

    // Fund the account ------------------------------------------------------------------------------------------------
    if (*(new_account->lamports) < funding_lamports) {
        uint64_t ret = util_transfer_lamports(funding_account_key, new_account->key,
                                              funding_lamports - *(new_account->lamports),
                                              transaction_accounts, transaction_accounts_len);
        if (ret) {
            return ret;
        }
    }

    // Allocate the account --------------------------------------------------------------------------------------------
    if (new_account->data_len != space) {
        // If the account is owned by the system program, then use the system Alloc instruction to allocate space
        if (is_system_program(new_account->owner)) {
            SolAccountMeta account_metas[] = 
                  ///   0. `[WRITE, SIGNER]` New account
                { { /* pubkey */ new_account->key, /* is_writable */ true, /* is_signer */ true } };
        
            instruction.accounts = account_metas;
            instruction.account_len = ARRAY_LEN(account_metas);
            
            util_AssignData data = { 8, space };
            
            instruction.data = (uint8_t *) &data;
            instruction.data_len = sizeof(data);
            
            uint64_t ret = sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len,
                                             &signer_seeds, 1);
            if (ret) {
                return ret;
            }
        }
        // Realloc the data segment so that the newly sized data segment is available.  If the account is not owned by
        // the program itself at the conclusion of this function, this instruction will fail when the runtime detects
        // the change by a non-owner
        set_account_size(new_account, space);
    }

    // Assign the account ----------------------------------------------------------------------------------------------
    if (!SolPubkey_same(new_account->owner, owner_account_key)) {
        // This will only succeed if the existing owner is the system program; if it's any other program, this
        // will fail
        SolAccountMeta account_metas[] = 
            // Assigned account public key
            { { /* pubkey */ new_account->key, /* is_writable */ true, /* is_signer */ true } };
        
        instruction.accounts = account_metas;
        instruction.account_len = ARRAY_LEN(account_metas);

        util_AssignData data = { 1, *owner_account_key };
        
        instruction.data = (uint8_t *) &data;
        instruction.data_len = sizeof(data);

        uint64_t ret = sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len,
                                         &signer_seeds, 1);
        if (ret) {
            return ret;
        }
    }

    return 0;
}
