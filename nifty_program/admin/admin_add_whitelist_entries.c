#pragma once

#include "inc/block.h"
#include "util/util_whitelist.c"

// Instruction data type for AddWhitelistEntries instruction.
typedef struct
{
    // This is the instruction code for AddWhitelistEntries
    uint8_t instruction_code;

    // This is the number of whitelist entries to add
    uint16_t count;

    // These are the whitelist entries to add, up to 28 of them
    SolPubkey entries[28];

} AddWhitelistEntriesData;


// Creates a new block of entries
static uint64_t admin_add_whitelist_entries(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,   config_account,                ReadOnly,   NotSigner,  KnownAccount_ProgramConfig);
        DECLARE_ACCOUNT(1,   admin_account,                 ReadOnly,   Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,   funding_account,               ReadWrite,  Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(3,   block_account,                 ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(4,   whitelist_account,             ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(5,   system_program_account,        ReadOnly,   NotSigner,  KnownAccount_SystemProgram);
    }
    DECLARE_ACCOUNTS_NUMBER(6);

    // Ensure the the transaction has been authenticated by the admin
    if (!is_admin_account(config_account, admin_account->key)) {
        return Error_PermissionDenied;
    }

    // Ensure that the instruction data is the correct size
    if (params->data_len != sizeof(AddWhitelistEntriesData)) {
        return Error_InvalidDataSize;
    }

    // Cast to instruction data
    const AddWhitelistEntriesData *data = (AddWhitelistEntriesData *) params->data;

    // Validate that the entry count is not too large
    if (data->count >= ARRAY_LEN(data->entries)) {
        return Error_InvalidData_First;
    }

    // Create the whitelist entry account
    return add_whitelist_entries(whitelist_account, block_account, funding_account->key, data->count,
                                 data->entries, params->ka, params->ka_num);
}
