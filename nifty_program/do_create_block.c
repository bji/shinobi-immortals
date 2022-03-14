
// Transaction must be signed by the admin address contained within the Admin Identifier Account.

// Account references:
// 0. `[]` Program config account -- this must be g_program_config_account_address
// 1. `[SIGNER]` -- This must be the admin account
// 2. `[WRITE, SIGNER]` Funding account
// 3. `[]` The system account, so that cross-program invoke of the system account can be done
// 4. `[WRITE]` -- The block account address (this is the address that results from finding a program address
//                 using the group id and block id as seeds)

// instruction data type for CreateBlock instruction.
typedef struct
{
    // This is the instruction code for CreateBlock
    uint8_t instruction_code;

    // Number of lamports to transfer from the funding account to the newly created account.  Sufficient lamports must
    // be supplied to make the account rent exempt, or else the transaction will fail.  It is not possible to withdraw
    // lamports from the funding account, except when the account is deleted on a successful DeleteBlock instruction.
    uint64_t funding_lamports;

    // The Program Derived Account address is generated from two 32 bit numbers.  The first is a block group id.  The
    // second is a block id within that group.  These two numbers together (in their little endian encoding) form the
    // seed that is used to generate the address of the block (using whatever bump seed first computes a valid PDA).
    // If the PDA that is derived from these seeds is invalid, the transaction fails.
    uint32_t group_id;
    uint32_t block_id;
    
    // Total number of entries in this block.  Must be greater than 0.
    uint16_t total_entry_count;
    
    // This is the number of bytes in mint_data for each BlockEntry in this block
    uint16_t mint_data_size;
    
} CreateBlockData;
    

// Creates a new block of entries
static uint64_t do_create_block(SolParameters *params)
{
    sol_log_params(params);
    
    // Sanitize the accounts.  There must be 5.
    if (params->ka_num != 4) {
        return Error_IncorrectNumberOfAccounts;
    }

    SolAccountInfo *config_account = &(params->ka[0]);
    SolAccountInfo *admin_account = &(params->ka[1]);
    SolAccountInfo *funding_account = &(params->ka[2]);
    SolAccountInfo *system_account = &(params->ka[3]);
    SolAccountInfo *block_account = &(params->ka[4]);
    
    // Ensure the the caller is the admin user
    if (!is_admin_authenticated(config_account, admin_account)) {
        return Error_PermissionDenied;
    }

    // Ensure funding account is a signer and is writable, but not executable
    if (!funding_account->is_signer || !funding_account->is_writable || funding_account->executable) {
        return Error_InvalidFundingAccount;
    }

    // Ensure that the system account reference was for the actual system account and that it has the correct flags
    if (!is_system_program(system_account->key)) {
        return Error_InvalidSystemAccount;
    }

    // Ensure that the instruction data is the correct size
    if (params->data_len != sizeof(CreateBlockData)) {
        return Error_ShortData;
    }

    // Cast to instruction data
    CreateBlockData *data = (CreateBlockData *) params->data;

    // Compute the address of the block
    SolPubkey block_address;
    if (!find_block_address(data->group_id, data->block_id, &block_address)) {
        return Error_InvalidBlockAddressSeeds;
    }

    // Ensure that the block account reference was for the correctly derived PDA
    if (sol_memcmp(block_account->key, &block_address, sizeof(SolPubkey))) {
        sol_log_pubkey(&block_address);
        return Error_InvalidBlockAccount;
    }

    uint64_t space = compute_block_size(data->mint_data_size, data->total_entry_count);

    sol_log("sizeof block");
    sol_log_64(0, 0, 0, 0, space);

    // Two seeds: the group id and the block id
    SolSignerSeed seeds[2] =
        { // First is group id
            { (uint8_t *) &(data->group_id), 4 },
            // Second is block id
            { (uint8_t *) &(data->block_id), 4 }
        };
    if (!create_account_with_seeds(block_account, seeds, 2, funding_account, (SolPubkey *) params->program_id,
                                   data->funding_lamports, space, params->ka, params->ka_num)) {
        return Error_CreateAccountFailed;
    }

    // The program has succeeded.  Return success code 0.
    return 0;
}
