#pragma once

#include "solana_sdk.h"

#include "inc/constants.h"
#include "inc/error.h"
#include "inc/program_config.h"
#include "inc/types.h"
#include "util/util_accounts.c"
#include "util/util_authentication.c"
#include "util/util_rent.c"
#include "util/util_stake.c"
#include "util/util_token.c"


// Account references:
// 0. `[SIGNER]` -- Super user account
// 1. `[WRITE]` -- The config account
// 2. `[WRITE]` -- The authority account
// 3. `[WRITE]` -- The master stake account
// 4. `[]` -- The Shinobi systems vote account
// 5. `[WRITE]` -- The Ki mint account
// 6. `[WRITE]` -- The Ki mint metadata account
// 7. `[]` -- The clock sysvar
// 8. `[]` -- The rent sysvar
// 9. `[]` -- The stake history sysvar
// 10. `[]` -- The stake config account
// 11. `[]` -- The system program
// 12. `[]` -- The stake program
// 13. `[]` -- The SPL-token program
// 14. `[]` -- The Metaplex metadata program


// Data passed to this program for Initialize function
typedef struct
{
    uint8_t instruction_code;

    SolPubkey admin_pubkey;
    
} InitializeData;


static uint64_t super_initialize(SolParameters *params)
{
    // Sanitize the accounts.  There must be exactly 15.
    if (params->ka_num != 15) {
        return Error_IncorrectNumberOfAccounts;
    }

    SolAccountInfo *superuser_account = &(params->ka[0]);
    SolAccountInfo *config_account = &(params->ka[1]);
    SolAccountInfo *authority_account = &(params->ka[2]);
    SolAccountInfo *master_stake_account = &(params->ka[3]);
    SolAccountInfo *shinobi_systems_vote_account = &(params->ka[4]);
    SolAccountInfo *ki_mint_account = &(params->ka[5]);
    SolAccountInfo *ki_mint_metadata_account = &(params->ka[6]);
    SolAccountInfo *clock_sysvar_account = &(params->ka[7]);
    SolAccountInfo *rent_sysvar_account = &(params->ka[8]);
    SolAccountInfo *stake_history_sysvar_account = &(params->ka[9]);
    SolAccountInfo *stake_config_account = &(params->ka[10]);
    // The system program is index 11, but there's no need to check it, because if it's not the system account,
    // a cross-program invoke will fail
    // The stake program is index 12, but there's no need to check it, because if it's not the system account,
    // a cross-program invoke will fail
    // The SPL-Token program is index 13, but there's no need to check it, because if it's not the system account,
    // a cross-program invoke will fail
    // The Metaplex metadata program is index 14, but there's no need to check it, because if it's not the system
    // account, a cross-program invoke will fail

    // Ensure that the input data is the correct size
    if (params->data_len != sizeof(InitializeData)) {
        return Error_InvalidDataSize;
    }

    InitializeData *data = (InitializeData *) params->data;
    
    // This instruction can only be executed by the authenticated superuser
    if (!is_superuser_authenticated(superuser_account)) {
        return Error_PermissionDenied;
    }

    // Check remaining account permissions
    if (!config_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 1;
    }
    if (!authority_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 2;
    }
    if (!master_stake_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 3;
    }
    if (!ki_mint_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 4;
    }
    if (!ki_mint_metadata_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 5;
    }

    // Ensure that the config account is the second account
    if (!is_nifty_config_account(config_account->key)) {
        return Error_InvalidAccount_First + 1;
    }

    // If the config account already exists and with the correct owner, then fail, because can't re-create the
    // config account, can only modify it after it's created
    if ((config_account->data_len > 0) && is_nifty_program(config_account->owner)) {
        return Error_InvalidAccount_First + 1;
    }
    
    // Ensure that the correct authority account was provided
    if (!is_nifty_authority_account(authority_account->key)) {
        return Error_InvalidAccount_First + 2;
    }

    // Ensure that the correct master stake account was provided
    if (!is_master_stake_account(master_stake_account->key)) {
        return Error_InvalidAccount_First + 3;
    }

    // Ensure that the correct Shinobi Systems vote account was provided
    if (!is_shinobi_systems_vote_account(shinobi_systems_vote_account->key)) {
        return Error_InvalidAccount_First + 4;
    }

    // Ensure that the correct ki mint account was provided
    if (!is_ki_mint_account(ki_mint_account->key)) {
        return Error_InvalidAccount_First + 5;
    }
    
    // Ensure that the correct ki mint metaplex metadata account was provided
    if (!SolPubkey_same(&(Constants.ki_mint_metadata_pubkey), ki_mint_metadata_account->key)) {
        return Error_InvalidAccount_First + 6;
    }

    // Ensure that the correct clock sysvar account was provided
    if (!SolPubkey_same(&(Constants.clock_sysvar_pubkey), clock_sysvar_account->key)) {
        return Error_InvalidAccount_First + 7;
    }
    
    // Ensure that the correct rent sysvar account was provided
    if (!SolPubkey_same(&(Constants.rent_sysvar_pubkey), rent_sysvar_account->key)) {
        return Error_InvalidAccount_First + 8;
    }
    
    // Ensure that the correct stake history sysvar account was provided
    if (!SolPubkey_same(&(Constants.stake_history_sysvar_pubkey), stake_history_sysvar_account->key)) {
        return Error_InvalidAccount_First + 9;
    }
    
    // Ensure that the correct stake config account was provided
    if (!SolPubkey_same(&(Constants.stake_config_pubkey), stake_config_account->key)) {
        return Error_InvalidAccount_First + 10;
    }
    
    // Create the config account.  The config account is derived from a fixed seed.
    {
        // The config account is allocated at the largest possible PDA account size of 10K.  This is large enough for
        // 318 metadata program ids to be added.  Surely this is enough for all time, but if this isn't enough, then
        // hopefully PDA account reallocation will be supported by the network in time.
        uint64_t account_size = 10 * 1024;

        uint8_t *seed_bytes = (uint8_t *) Constants.nifty_config_seed_bytes;
        SolSignerSeed seed = { seed_bytes, sizeof(Constants.nifty_config_seed_bytes) };

        if (create_pda(config_account, &seed, 1, superuser_account->key, (SolPubkey *) params->program_id,
                       get_rent_exempt_minimum(account_size), account_size, params->ka, params->ka_num)) {
            return Error_CreateAccountFailed;
        }

        // Set the initial contents of the config account.
        ProgramConfig *config = (ProgramConfig *) (config_account->data);
        config->data_type = DataType_ProgramConfig;
        config->admin_pubkey = data->admin_pubkey;
    }

    // Create the authority account.  The authority account is derived from a fixed seed and doesn't hold any data.
    {
        uint8_t *seed_bytes = (uint8_t *) Constants.nifty_authority_seed_bytes;
        SolSignerSeed seed = { seed_bytes, sizeof(Constants.nifty_authority_seed_bytes) };
        
        if (create_pda(authority_account, &seed, 1, superuser_account->key, &(Constants.nifty_program_pubkey),
                       get_rent_exempt_minimum(0), 0, params->ka, params->ka_num)) {
            return Error_CreateAccountFailed;
        }
    }

    // Create the master stake account
    {
        uint8_t *seed_bytes = (uint8_t *) Constants.master_stake_seed_bytes;
        SolSignerSeed seed = { seed_bytes, sizeof(Constants.master_stake_seed_bytes) };
        
        if (!create_stake_account(master_stake_account, &seed, 1, superuser_account->key,
                                  MASTER_STAKE_ACCOUNT_MIN_LAMPORTS, &(Constants.nifty_authority_pubkey),
                                  &(Constants.nifty_authority_pubkey), params->ka, params->ka_num)) {
            return Error_CreateAccountFailed;
        }
    }

    // Delegate master stake account to Shinobi Systems
    if (!delegate_stake_signed(master_stake_account->key, &(Constants.shinobi_systems_vote_pubkey),
                               params->ka, params->ka_num)) {
        return Error_FailedToDelegate;
    }

    // Create the Ki mint
    {
        uint8_t *seed_bytes = (uint8_t *) Constants.ki_mint_seed_bytes;
        SolSignerSeed seed = { seed_bytes, sizeof(Constants.ki_mint_seed_bytes) };

        if (create_token_mint(ki_mint_account, &seed, 1, &(Constants.nifty_authority_pubkey), superuser_account->key,
                              params->ka, params->ka_num)) {
            return Error_CreateAccountFailed;
        }
    }

    // Create the metadata for the Ki mint
    uint8_t *name = (uint8_t *) KI_TOKEN_NAME;
    uint8_t *symbol = (uint8_t *) KI_TOKEN_SYMBOL;
    uint8_t *uri = (uint8_t *) KI_TOKEN_METADATA_URI;
    if (create_metaplex_metadata(&(Constants.ki_mint_metadata_pubkey), &(Constants.ki_mint_pubkey),
                                 &(Constants.nifty_authority_pubkey), superuser_account->key, name, symbol, uri,
                                 &(Constants.shinobi_systems_vote_pubkey), &(Constants.system_program_pubkey),
                                 params->ka, params->ka_num)) {
        return Error_CreateAccountFailed;
    }

    return 0;
}
