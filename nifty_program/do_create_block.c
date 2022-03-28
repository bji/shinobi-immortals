
// Transaction must be signed by the admin address contained within the Admin Identifier Account.

// Account references:
// 0. `[]` Program config account -- this must be g_program_config_account_address
// 1. `[SIGNER]` -- This must be the admin account
// 2. `[WRITE, SIGNER]` Funding account
// 3. `[WRITE]` -- The block account address
// 4. `[]` The system account, so that cross-program invoke of the system account can be done

// instruction data type for CreateBlock instruction.
typedef struct
{
    // This is the instruction code for CreateBlock
    uint8_t instruction_code;

    // Initial commission to use in the newly created block
    commission_t initial_commission;

    // Number of lamports to transfer from the funding account to the newly created account.  Sufficient lamports must
    // be supplied to make the account rent exempt, or else the transaction will fail.  It is not possible to withdraw
    // lamports from the funding account, except when the account is deleted on a successful DeleteBlock instruction.
    uint64_t funding_lamports;

    // The Program Derived Account address is generated using 32 seed bytes, these are those seed bytes
    uint8_t seed[32];

    // The actual configuration of the block is provided
    BlockConfiguration config;
    
} CreateBlockData;
    

static uint64_t compute_block_size(uint16_t total_entry_count)
{
    Block *b = 0;

    // The total space needed is from the beginning of a Block to the entries element one beyond the total
    // supported (i.e. if there are 100 entries, then then entry at index 100 starts at the first byte beyond the
    // array)
    return ((uint64_t) &(b->entries[total_entry_count]));
}


// Creates a new block of entries
static uint64_t do_create_block(SolParameters *params)
{
    // Sanitize the accounts.  There must be 5.
    if (params->ka_num != 5) {
        return Error_IncorrectNumberOfAccounts;
    }

    SolAccountInfo *config_account = &(params->ka[0]);
    SolAccountInfo *admin_account = &(params->ka[1]);
    SolAccountInfo *funding_account = &(params->ka[2]);
    SolAccountInfo *block_account = &(params->ka[3]);
    // The fifth account is the system account but there's no need to check that; the create_pda call will simply
    // fail if the 5th account is not the system account
    
    // Ensure the the transaction has been authenticated by the admin
    if (!is_admin_authenticated(config_account, admin_account)) {
        return Error_PermissionDenied;
    }

    // Ensure funding account is a signer and is writable, but not executable
    if (!funding_account->is_signer || !funding_account->is_writable || funding_account->executable) {
        return Error_InvalidAccountPermissions_First + 2;
    }

    // Ensure that the instruction data is the correct size
    if (params->data_len != sizeof(CreateBlockData)) {
        return Error_InvalidDataSize;
    }

    // Cast to instruction data
    CreateBlockData *data = (CreateBlockData *) params->data;

    BlockConfiguration *config = &(data->config);
    
    // Sanitize inputs
    if (config->total_entry_count == 0) {
        return Error_InvalidData_First + 0;
    }
    if (config->reveal_ticket_count > config->total_entry_count) {
        return Error_InvalidData_First + 1;
    }
    if (config->ticket_price_lamports == 0) {
        return Error_InvalidData_First + 2;
    }
    if (config->ticket_refund_lamports > config->ticket_price_lamports) {
        return Error_InvalidData_First + 3;
    }
    if (config->bid_minimum == 0) {
        return Error_InvalidData_First + 4;
    }
    if (config->principal_withdraw_commission == 0) {
        return Error_InvalidData_First + 5;
    }

    // This is the size that the fully populated block will use.  Because accounts cannot currently be appended to,
    // pre-allocate the total amount.
    uint64_t total_block_size = compute_block_size(data->config.total_entry_count);

    SolSignerSeed seed = { data->seed, sizeof(data->seed) };

    if (create_pda(block_account, &seed, 1, funding_account, (SolPubkey *) params->program_id, data->funding_lamports,
                   total_block_size, params->ka, params->ka_num)) {
        return Error_CreateAccountFailed;
    }

    // The program has succeeded.  Initialize the block data.
    Block *block = (Block *) (block_account->data);

    block->data_type = DataType_Block;

    sol_memcpy(&(block->config), config, sizeof(BlockConfiguration));

    // Set the commission to the initial commission
    block->state.commission = data->initial_commission;

    // Return success code 0
    return 0;
}
