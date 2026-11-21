#pragma once

#include "inc/block.h"
#include "util/util_entry.c"
#include "util/util_rent.c"
#include "util/util_whitelist.c"

// instruction data type for CreateBlock instruction.
typedef struct
{
    // This is the instruction code for CreateBlock
    uint8_t instruction_code;

    // The actual configuration of the block is provided
    BlockConfiguration config;

} CreateBlockData;


// Creates a new block of entries
static uint64_t admin_create_block(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,   state_account,                 ReadOnly,   NotSigner,  KnownAccount_ProgramState);
        DECLARE_ACCOUNT(1,   admin_account,                 ReadOnly,   Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,   funding_account,               ReadWrite,  Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(3,   block_account,                 ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(4,   system_program_account,        ReadOnly,   NotSigner,  KnownAccount_SystemProgram);
        // The block's whitelist account.  Only used, and only checked, if the block uses a whitelist
        // (whitelist_duration > 0); otherwise any account may be supplied.
        DECLARE_ACCOUNT(5,   whitelist_account,             ReadOnly,   NotSigner,  KnownAccount_NotKnown);
    }
    DECLARE_ACCOUNTS_NUMBER(6);

    // Ensure the the transaction has been authenticated by the admin
    if (!is_admin_account(state_account, admin_account->key)) {
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
    const CreateBlockData *data = (CreateBlockData *) params->data;

    const BlockConfiguration *config = &(data->config);

    // Sanitize inputs
    if (config->total_entry_count == 0) {
        return Error_InvalidData_First + 0;
    }
    // There cannot be more mysteries than entries.  This is checked regardless of the mystery phase duration.
    if (config->total_mystery_count > config->total_entry_count) {
        return Error_InvalidData_First + 1;
    }
    // If there are mysteries, then mystery values apply
    if (config->total_mystery_count > 0) {
        // Mysteries are sold during the mystery phase, so there must be one.  (A zero duration would make the block
        // revealable one second after it is completed, while leaving mysteries purchasable in that second.)  The
        // converse, a nonzero duration with no mysteries, is allowed: the duration then has no effect.
        if (config->mystery_phase_duration == 0) {
            return Error_InvalidData_First + 8;
        }
        // Ensure that the initial mystery price is no more than 100,000,000 SOL to avoid overflow
        if (config->mystery_start_price_lamports > (100ul * 1000ul * 1000ul * LAMPORTS_PER_SOL)) {
            return Error_InvalidData_First + 2;
        }
        // Ensure that the final mystery price is not greater than the initial mystery price
        if (config->minimum_price_lamports > config->mystery_start_price_lamports) {
            return Error_InvalidData_First + 3;
        }
    }

    // (A block has an auction exactly when its auction_duration is nonzero, so an auction always has a nonzero
    // duration.  A block without an auction may have a final_sale_duration of 0, in which case a revealed entry is sold
    // at minimum_price_lamports from the start.)

    // Ensure that the final start price is no more than 100,000,000 SOL, to avoid overflow
    if (config->final_start_price_lamports > (100ul * 1000ul * 1000ul * LAMPORTS_PER_SOL)) {
        return Error_InvalidData_First + 5;
    }
    // Ensure that the final start price is >= the minimum price
    if (config->final_start_price_lamports < config->minimum_price_lamports) {
        return Error_InvalidData_First + 6;
    }

    // Ensure that the minimum price of an entry is >= rent exempt minimum of a bid account, to ensure that bids
    // can always be created
    if (config->minimum_price_lamports < get_rent_exempt_minimum(sizeof(Bid))) {
        return Error_InvalidData_First + 7;
    }

    // If the block uses a whitelist, then its whitelist must already exist and have entries in it.  A whitelist can
    // only be created and added to before its block is created (see add_whitelist_entries), and a non-empty whitelist
    // cannot be deleted from the time its block is created until the block's whitelist period is over (see
    // delete_whitelist_account).  So with this check, a block that uses a whitelist always has one, fixed from the
    // moment the block is created, for its entire whitelist period; rather than having no whitelist at all, which
    // would leave it open to everyone (see whitelist_check).
    if (config->whitelist_duration > 0) {
        // The whitelist account must be the one at this block's whitelist address
        SolPubkey whitelist_address;
        if (!compute_whitelist_address(block_account->key, &whitelist_address) ||
            !SolPubkey_same(whitelist_account->key, &whitelist_address)) {
            return Error_InvalidAccount_First + 5;
        }
        // And it must be a valid whitelist with entries in it
        const Whitelist *whitelist = get_validated_whitelist(whitelist_account);
        if (!whitelist || (whitelist->count == 0)) {
            return Error_WhitelistRequired;
        }
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

    return 0;
}
