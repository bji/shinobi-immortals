
// Transaction must be signed by the admin address contained within the Admin Identifier Account.

// Account references:
// 0. `[]` Program config account -- this must be g_program_config_account_address
// 1. `[SIGNER]` -- This must be the admin account
// 2. `[WRITE]` -- The block account address (this is the address that results from finding a program address
//                 using the group id and block id as seeds)

// instruction data type for CreateBlock instruction.
typedef struct
{
    // This is the instruction code for CreateBlock
    uint8_t instruction_code;

    // The BlockConfiguration
    BlockConfiguration config;
    
} InitializeBlockData;
    

// Creates a new block of entries
static uint64_t do_initialize_block(SolParameters *params)
{
    sol_log_params(params);
    
    // Sanitize the accounts.  There must be 3.
    if (params->ka_num != 3) {
        return Error_IncorrectNumberOfAccounts;
    }

    SolAccountInfo *config_account = &(params->ka[0]);
    SolAccountInfo *admin_account = &(params->ka[1]);
    SolAccountInfo *block_account = &(params->ka[2]);
    
    // Ensure the the caller is the admin user
    if (!is_admin_authenticated(config_account, admin_account)) {
        return Error_PermissionDenied;
    }

    // Ensure that the instruction data is the correct size
    if (params->data_len != sizeof(InitializeBlockData)) {
        return Error_ShortData;
    }

    // Cast to instruction data
    InitializeBlockData *data = (InitializeBlockData *) params->data;

    BlockConfiguration *config = &(data->config);

    // Compute the address of the block
    SolPubkey block_address;
    if (!find_block_address(config->group_id, config->block_id, &block_address)) {
        return Error_InvalidBlockAddressSeeds;
    }

    // Ensure that the block account reference was for the correctly derived PDA
    if (sol_memcmp(block_account->key, &block_address, sizeof(SolPubkey))) {
        sol_log_pubkey(&block_address);
        return Error_InvalidBlockAccount;
    }

    // Ensure that the block account reference was for the correctly derived PDA
    if (sol_memcmp(block_account->key, &block_address, sizeof(SolPubkey))) {
        sol_log_pubkey(&block_address);
        return Error_InvalidBlockAccount;
    }

    // Sanitize other inputs
    if (config->total_entry_count == 0) {
        return Error_InvalidInputData_First + 0;
    }
    if (config->reveal_ticket_count > config->total_entry_count) {
        return Error_InvalidInputData_First + 1;
    }
    if (config->ticket_price_lamports == 0) {
        return Error_InvalidInputData_First + 2;
    }
    if (config->ticket_refund_lamports > config->ticket_price_lamports) {
        return Error_InvalidInputData_First + 3;
    }
    if (config->bid_minimum == 0) {
        return Error_InvalidInputData_First + 4;
    }
    if (config->permabuy_commission == 0) {
        return Error_InvalidInputData_First + 5;
    }

    uint64_t space = compute_block_size(config->mint_data_size, config->total_entry_count);

    // Ensure that the account has exactly this much space
    if (block_account->data_len != space) {
        return Error_InvalidDataLen;
    }

    // Store the configuration into the block data
    sol_memcpy(block_account->data, config, sizeof(BlockConfiguration));

    // The program has succeeded.  Return success code 0.
    //return 0;
    return 2000;
}
