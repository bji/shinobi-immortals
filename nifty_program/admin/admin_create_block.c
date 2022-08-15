#pragma once

#include "inc/block.h"
//#include "util/util_accounts.c"
#include "util/util_entry.c"
#include "util/util_rent.c"

// instruction data type for CreateBlock instruction.
typedef struct
{
    // This is the instruction code for CreateBlock
    uint8_t instruction_code;

    // Initial commission to use in the newly created block
    commission_t initial_commission;

    // The actual configuration of the block is provided
    BlockConfiguration config;

} CreateBlockData;
    

// Creates a new block of entries
static uint64_t admin_create_block(SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,   config_account,                ReadOnly,   NotSigner,  KnownAccount_ProgramConfig);
        DECLARE_ACCOUNT(1,   admin_account,                 ReadOnly,   Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,   funding_account,               ReadWrite,  Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(3,   block_account,                 ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(4,   system_program_account,        ReadOnly,   NotSigner,  KnownAccount_SystemProgram);
    }
    DECLARE_ACCOUNTS_NUMBER(5);
    
    // Ensure the the transaction has been authenticated by the admin
    if (!is_admin_account(config_account, admin_account->key)) {
        return Error_PermissionDenied;
    }

    // If the block exists and is a valid block, then fail - can't re-create a valid block
    if (get_validated_block(block_account)) {
        return Error_InvalidAccount_First + 3;
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
    // If there are mysteries and a nonzero mystery duration, then mystery values apply
    if ((config->total_mystery_count > 0) && (config->mystery_phase_duration > 0)) {
        if (config->total_mystery_count > config->total_entry_count) {
            return Error_InvalidData_First + 1;
        }
        // Ensure that the initial mystery price is no more than 100,000 SOL, to avoid rounding errors in mystery
        // price computation
        if (config->mystery_start_price_lamports > (100ull * 1000ull * LAMPORTS_PER_SOL)) {
            return Error_InvalidData_First + 2;
        }
        // Ensure that the final mystery price is not greater than the initial mystery price
        if (config->minimum_price_lamports > config->mystery_start_price_lamports) {
            return Error_InvalidData_First + 3;
        }
    }

    if (config->has_auction) {
        // Auctions must have nonzero duration
        if (config->duration == 0) {
            return Error_InvalidData_First + 4;
        }
    }
    else {
        // Ensure that the post-mystery start price is no more than 100,000 SOL, to avoid rounding errors in price
        // calculations
        if (config->non_auction_start_price_lamports > (100ull * 1000ull * LAMPORTS_PER_SOL)) {
            return Error_InvalidData_First + 5;
        }
        // Ensure that the post-mystery start price is >= the minimum price
        if (config->non_auction_start_price_lamports < config->minimum_price_lamports) {
            return Error_InvalidData_First + 6;
        }
    }
    
    // Ensure that the minimum price of an entry is >= rent exempt minimum of a bid account, to ensure that bids
    // can always be created
    if (config->minimum_price_lamports < get_rent_exempt_minimum(sizeof(Bid))) {
        return Error_InvalidData_First + 7;
    }

    // Create the block account
    uint64_t ret = create_block_account(block_account, config->group_number, config->block_number,
                                        config->total_entry_count, funding_account->key, params->ka, params->ka_num);
    if (ret) {
        return ret;
    }

    // The program has succeeded.  Initialize the block data.
    Block *block = (Block *) (block_account->data);

    block->data_type = DataType_Block;

    block->config = *config;

    block->commission = data->initial_commission;

    return 0;
}
