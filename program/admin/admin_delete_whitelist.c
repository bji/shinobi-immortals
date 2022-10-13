#pragma once

#include "inc/block.h"
#include "util/util_whitelist.c"

// instruction data type for DeleteWhitelist instruction.
typedef struct
{
    // This is the instruction code for DeleteWhitelist
    uint8_t instruction_code;

} DeleteWhitelistData;


// Deletes a whitelist that is no longer needed
static uint64_t admin_delete_whitelist(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,   config_account,                ReadOnly,   NotSigner,  KnownAccount_ProgramConfig);
        DECLARE_ACCOUNT(1,   admin_account,                 ReadWrite,  Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,   block_account,                 ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(3,   whitelist_account,             ReadWrite,  NotSigner,  KnownAccount_NotKnown);
    }
    DECLARE_ACCOUNTS_NUMBER(4);

    // Ensure the the transaction has been authenticated by the admin
    if (!is_admin_account(config_account, admin_account->key)) {
        return Error_PermissionDenied;
    }

    // Ensure that the instruction data is the correct size
    if (params->data_len != sizeof(DeleteWhitelistData)) {
        return Error_InvalidDataSize;
    }

    // Get the clock sysvar, needed below
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }

    // Delete the whitelist account if the conditions are correct for doing so, returning the lamports to the
    // admin account
    return delete_whitelist_account(whitelist_account, block_account, &clock, admin_account,
                                    params->ka, params->ka_num);
}
