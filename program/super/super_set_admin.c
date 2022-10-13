#pragma once

#include "solana_sdk.h"

#include "inc/constants.h"
#include "inc/error.h"
#include "inc/instruction_accounts.h"
#include "inc/program_config.h"
#include "util/util_accounts.c"

// Data passed to this program for UpdateAdmin function
typedef struct
{
    uint8_t instruction_code;

    SolPubkey admin_pubkey;

} UpdateAdminData;


static uint64_t super_set_admin(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,   superuser_account,             ReadOnly,   Signer,     KnownAccount_SuperUser);
        DECLARE_ACCOUNT(1,   config_account,                ReadWrite,  NotSigner,  KnownAccount_ProgramConfig);
    }
    DECLARE_ACCOUNTS_NUMBER(2);

    // Ensure that the input data is the correct size
    if (params->data_len != sizeof(UpdateAdminData)) {
        return Error_InvalidDataSize;
    }

    const UpdateAdminData *data = (UpdateAdminData *) params->data;

    // Ensure that the config account data is the correct size
    if (config_account->data_len != sizeof(ProgramConfig)) {
        return Error_InvalidAccount_First + 1;
    }

    ProgramConfig *config = (ProgramConfig *) (config_account->data);

    // Ensure that the config account is of the correct type
    if (config->data_type != DataType_ProgramConfig) {
        return Error_InvalidAccount_First + 1;
    }

    // Update the admin pubkey
    config->admin_pubkey = data->admin_pubkey;

    return 0;
}
