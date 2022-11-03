#pragma once

#include "inc/block.h"
#include "util/util_whitelist.c"

#define MAX_WHITELIST_ADD_ENTRIES 28

// Instruction data type for AddWhitelistEntries instruction.
typedef struct
{
    // This is the instruction code for AddWhitelistEntries
    uint8_t instruction_code;

    // This is the number of whitelist entries to add
    uint16_t count;

    // These are the whitelist entries to add, up to MAX_WHITELIST_ADD_ENTRIES of them
    SolPubkey entries[];

} AddWhitelistEntriesData;


static uint64_t add_whitelist_entries_data_size(uint16_t count)
{
    AddWhitelistEntriesData *zero = 0;

    return (uint64_t) &(zero->entries[count]);
}


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

    // Ensure that the instruction data is at least the minimum size
    if (params->data_len < sizeof(AddWhitelistEntriesData)) {
        return Error_InvalidDataSize;
    }

    // Cast to instruction data
    const AddWhitelistEntriesData *data = (AddWhitelistEntriesData *) params->data;

    // Ensure that the count is not too small or large
    if ((data->count == 0) || (data->count > MAX_WHITELIST_ADD_ENTRIES)) {
        return Error_InvalidData_First;
    }

    // Ensure that the instruction data is the correct size
    if (add_whitelist_entries_data_size(data->count) != params->data_len) {
        return Error_InvalidDataSize;
    }

    // Create the whitelist entry account
    return add_whitelist_entries(whitelist_account, block_account, funding_account->key, data->count,
                                 data->entries, params->ka, params->ka_num);
}
