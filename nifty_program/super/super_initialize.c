#include "solana_sdk.h"

#include "inc/error.h"
#include "inc/program_config.h"
#include "inc/types.h"
#include "util/util_accounts.c"
#include "util/util_authentication.c"
#include "util/util_rent.c"


// Account references:
// 0. `[SIGNER]` -- Super user account
// 1. `[WRITE]` -- The config account
// 2. `[WRITE]` -- The authority account
// 3. `[]` -- The system program


// Data passed to this program for Initialize function
typedef struct
{
    uint8_t instruction_code;

    SolPubkey admin_pubkey;
    
} InitializeData;


static uint64_t super_initialize(SolParameters *params)
{
    // Ensure that the input data is the correct size
    if (params->data_len != sizeof(InitializeData)) {
        return Error_InvalidDataSize;
    }

    InitializeData *data = (InitializeData *) params->data;
    
    // Sanitize the accounts.  There must be exactly 4.
    if (params->ka_num != 4) {
        return Error_IncorrectNumberOfAccounts;
    }

    SolAccountInfo *superuser_account = &(params->ka[0]);
    SolAccountInfo *config_account = &(params->ka[1]);
    SolAccountInfo *authority_account = &(params->ka[2]);
    // The system program is index 3, but there's no need to check it, because if it's not the system account,
    // then the create_pda call below will fail and the transaction will fail

    // This instruction can only be executed by the authenticated superuser
    if (!is_superuser_authenticated(superuser_account)) {
        return Error_PermissionDenied;
    }

    // Ensure that the config account is the second account and that it is writable
    if (!is_nifty_config_account(config_account->key) || !config_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 1;
    }

    // Ensure that the authority account is the third account and that it is writable
    if (!is_nifty_authority_account(authority_account->key) || !authority_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 2;
    }
    
    // Create the config account.  The config account is derived from a fixed seed.
    {
        // The config account is allocated at the largest possible PDA account size of 10K.  This is large enough for
        // 318 metadata program ids to be added.  Surely this is enough for all time, but if this isn't enough, then
        // hopefully PDA account reallocation will be supported by the network in time.
        uint64_t account_size = 10 * 1024;

        uint8_t *seed_bytes = (uint8_t *) Constants.nifty_config_seed_bytes;

        SolSignerSeed seed = { seed_bytes, sizeof(Constants.nifty_config_seed_bytes) };

        if (create_pda(config_account->key, &seed, 1, superuser_account->key, (SolPubkey *) params->program_id,
                       get_rent_exempt_minimum(account_size), account_size, params->ka, params->ka_num)) {
            return Error_CreateAccountFailed;
        }
    }

    // Set the initial contents of the config account.

    ProgramConfig *config = (ProgramConfig *) (config_account->data);

    config->data_type = DataType_ProgramConfig;

    config->admin_pubkey = data->admin_pubkey;

    // Create the authority account.  The authority account is derived from a fixed seed.
    {
        // The authority account doesn't hold any data
        uint64_t account_size = 0;

        uint8_t *seed_bytes = (uint8_t *) Constants.nifty_authority_seed_bytes;
        
        SolSignerSeed seed = { seed_bytes, sizeof(Constants.nifty_authority_seed_bytes) };
        
        if (create_pda(authority_account->key, &seed, 1, superuser_account->key, (SolPubkey *) params->program_id,
                       get_rent_exempt_minimum(account_size), account_size, params->ka, params->ka_num)) {
            return Error_CreateAccountFailed;
        }
    }

    return 0;
}