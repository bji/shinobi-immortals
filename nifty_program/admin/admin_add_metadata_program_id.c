

#include "solana_sdk.h"

#include "inc/error.h"
#include "inc/program_config.h"
#include "util/util_accounts.c"
#include "util/util_authentication.c"

// Account references:
// 0. `[SIGNER]` -- admin account
// 1. `[WRITE]` -- config account


// Data passed to this program for AddMetadataProgramId function
typedef struct
{
    uint8_t instruction_code;
    
    SolPubkey metadata_program_id;
    
} AddMetadataProgramIdData;


static uint64_t admin_add_metadata_program_id(SolParameters *params)
{
    // Ensure that the input data is the correct size
    if (params->data_len != sizeof(AddMetadataProgramIdData)) {
        return Error_InvalidDataSize;
    }

    AddMetadataProgramIdData *data = (AddMetadataProgramIdData *) params->data;

    // Sanitize the accounts.  There must be exactly 2.
    if (params->ka_num != 2) {
        return Error_IncorrectNumberOfAccounts;
    }

    SolAccountInfo *config_account = &(params->ka[0]);
    SolAccountInfo *admin_account = &(params->ka[1]);

    // Ensure the the transaction has been authenticated by the admin
    if (!is_admin_authenticated(config_account, admin_account)) {
        return Error_PermissionDenied;
    }

    // Ensure that the config account is the second account and that it is writable
    if (!is_nifty_config_account(config_account->key) || !config_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 1;
    }

    // Ensure that the config account is of sufficient size
    if (config_account->data_len < sizeof(ProgramConfig)) {
        return Error_InvalidAccount_First + 1;
    }

    ProgramConfig *config = (ProgramConfig *) (config_account->data);

    // Ensure that the account is of the correct type
    if (config->data_type != DataType_ProgramConfig) {
        return Error_InvalidAccount_First + 1;
    }

    // If already maxed out number of metadata program ids, cannot proceed
    if (config->metadata_program_id_count == 0xFFFFFFFF) {
        return Error_InvalidData_First;
    }

    // This is the offset of the metadata program id within the erray
    SolPubkey *entry = &(config->metadata_program_id[config->metadata_program_id_count]);

    // If this is larger than the current size of the config account, attempt a realloc by updating the
    // account size
    uint64_t needed_size = ((uint64_t) entry) - (uint64_t) config;

    if (needed_size > config_account->data_len) {
        // Update the account data len; this will cause the transaction to fail the transaction if realloc is not
        // supported (yet)
        set_account_size(config_account, needed_size);
    }

    // Update the data
    *entry = data->metadata_program_id;
    config->metadata_program_id_count += 1;

    return 0;
}
