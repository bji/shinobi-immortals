#include "solana_sdk.h"

#include "inc/constants.h"
#include "inc/error.h"
#include "inc/program_config.h"
#include "util/util_accounts.c"
#include "util/util_authentication.c"

// Account references:
// 0. `[SIGNER]` -- Super user account
// 1. `[WRITE]` -- The config account
// TESTING OPTIONAL:
// 2. `[WRITE]` -- The authority account


// Data passed to this program for UpdateAdmin function
typedef struct
{
    uint8_t instruction_code;
    
    SolPubkey admin_pubkey;
    
} UpdateAdminData;


static uint64_t super_set_admin(SolParameters *params)
{
    // Ensure that the input data is the correct size
    if (params->data_len != sizeof(UpdateAdminData)) {
        return Error_InvalidDataSize;
    }

    UpdateAdminData *data = (UpdateAdminData *) params->data;

    // Sanitize the accounts.  There must be exactly 2.
    if (params->ka_num != 2) {
        return Error_IncorrectNumberOfAccounts;
    }

    SolAccountInfo *superuser_account = &(params->ka[0]);
    SolAccountInfo *config_account = &(params->ka[1]);

    // This instruction can only be executed by the authenticated superuser
    if (!is_superuser_authenticated(superuser_account)) {
        return Error_PermissionDenied;
    }

    // Ensure that the config account is the second account and that it is writable
    if (!is_nifty_config_account(config_account->key) || !config_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 1;
    }

    // Ensure that the data is of sufficient size
    if (config_account->data_len < sizeof(ProgramConfig)) {
        return Error_InvalidAccount_First + 1;
    }

    ProgramConfig *config = (ProgramConfig *) (config_account->data);

    // Ensure that the account is of the correct type
    if (config->data_type != DataType_ProgramConfig) {
        return Error_InvalidAccount_First + 1;
    }

    // Update the admin pubkey
    config->admin_pubkey = data->admin_pubkey;

    return 0;
}
