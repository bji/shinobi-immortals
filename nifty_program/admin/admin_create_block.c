
#include "inc/block.h"
#include "util/util_accounts.c"
#include "util/util_authentication.c"
#include "util/util_rent.c"

// Transaction must be signed by the admin address contained within the Admin Identifier Account.

// Account references:
// 0. `[]` Program config account -- this must be g_program_config_account_address
// 1. `[SIGNER]` -- This must be the admin account
// 2. `[WRITE, SIGNER]` Funding account
// 3. `[WRITE]` -- The block account address.  This must be the PDA derived from the following seed values
//       concatenated together: PDA_Account_Seed_Prefix_Block, config.group_number, config.block_number
// 4. `[]` The system account, so that cross-program invoke of the system account can be done

// instruction data type for CreateBlock instruction.
typedef struct
{
    // This is the instruction code for CreateBlock
    uint8_t instruction_code;

    // Initial commission to use in the newly created block
    commission_t initial_commission;

    // This is the bump seed to add to the sequence
    // "PDA_Account_Seed_Prefix_Block, config.group_number, config.block_number" to derive the PDA of the block
    uint8_t bump_seed;

    // The actual configuration of the block is provided
    BlockConfiguration config;
    
} CreateBlockData;
    

static uint64_t compute_block_size(uint16_t total_entry_count)
{
    Block *b = 0;

    // The total space needed is from the beginning of a Block to the entries element one beyond the total
    // supported (i.e. if there are 100 entries, then then entry at index 100 starts at the first byte beyond the
    // array)
    return ((uint64_t) &(b->entry_bump_seeds[total_entry_count]));
}


// Creates a new block of entries
static uint64_t admin_create_block(SolParameters *params)
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
    if (config->total_mystery_count > 0) {
        if (config->total_mystery_count > config->total_entry_count) {
            return Error_InvalidData_First + 1;
        }
        if (config->initial_mystery_price_lamports < config->minimum_bid_lamports) {
            return Error_InvalidData_First + 2;
        }
    }
    // Ensure that the minimum bid is at least the rent exempt minimum of a bid account
    if (config->minimum_bid_lamports < get_rent_exempt_minimum(sizeof(Bid))) {
        return Error_InvalidData_First + 3;
    }
    
    // This is the size that the fully populated block will use.
    uint64_t total_block_size = compute_block_size(data->config.total_entry_count);

    // Use the provided bump seed plus the deterministic seed parts to create the block account address
    uint8_t prefix = PDA_Account_Seed_Prefix_Block;
    SolSignerSeed seed_parts[] = { { &prefix, 1 },
                                   { (uint8_t *) &(data->config.group_number), sizeof(data->config.group_number) },
                                   { (uint8_t *) &(data->config.block_number), sizeof(data->config.block_number) },
                                   { &(data->bump_seed), sizeof(data->bump_seed) } };

    // Create the block account
    if (create_pda(block_account->key, seed_parts, sizeof(seed_parts) / sizeof(seed_parts[0]), funding_account->key,
                   (SolPubkey *) params->program_id, get_rent_exempt_minimum(total_block_size), total_block_size,
                   params->ka, params->ka_num)) {
        return Error_CreateAccountFailed;
    }

    // The program has succeeded.  Initialize the block data.
    Block *block = (Block *) (block_account->data);

    block->data_type = DataType_Block;

    sol_memcpy(&(block->config), config, sizeof(block->config));

    // Set the commission to the initial commission
    block->state.commission = data->initial_commission;
    
    return 0;
}
