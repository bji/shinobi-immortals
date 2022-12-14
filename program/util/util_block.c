#pragma once

#include "inc/block.h"
#include "util/util_accounts.c"


static uint64_t compute_block_size(uint16_t entry_count)
{
    Block *b = 0;

    return (uint64_t) &(b->entries_added_bitmap[(((uint32_t) entry_count + 1) / 8) + 1]);
}


// Returns an error if [block_account] is not the correct account
static uint64_t create_block_account(SolAccountInfo *block_account, uint32_t group_number,
                                     uint32_t block_number, uint16_t entry_count, const SolPubkey *funding_key,
                                     const SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    // Compute the block address
    uint8_t prefix = PDA_Account_Seed_Prefix_Block;

    uint8_t bump_seed;

    SolSignerSeed seeds[] = { { &prefix, sizeof(prefix) },
                              { (uint8_t *) &group_number, sizeof(group_number) },
                              { (uint8_t *) &block_number, sizeof(block_number) },
                              { &bump_seed, sizeof(bump_seed) } };

    SolPubkey pubkey;
    uint64_t ret = sol_try_find_program_address(seeds, ARRAY_LEN(seeds) - 1, &(Constants.self_program_pubkey),
                                                &pubkey, &bump_seed);
    if (ret) {
        return ret;
    }

    // Verify that the block address is as expected
    if (!SolPubkey_same(&pubkey, block_account->key)) {
        return Error_CreateAccountFailed;
    }

    // The size of the block to create is the sizeof a block + 1 byte per 8 entries (for the entries added bitmap)
    uint64_t block_size = compute_block_size(entry_count);

    return create_pda(block_account, seeds, ARRAY_LEN(seeds), funding_key, &(Constants.self_program_pubkey),
                      get_rent_exempt_minimum(block_size), block_size, transaction_accounts,
                      transaction_accounts_len);
}


// Given a block account, returns the validated Block or null if the block account is invalid in some way.
static Block *get_validated_block(const SolAccountInfo *block_account)
{
    // Make sure that the block account is owned by the program
    if (!is_self_program(block_account->owner)) {
        return 0;
    }

    // Block account must have at least enough size to hold zero entries
    if (block_account->data_len < sizeof(Block)) {
        return 0;
    }

    const Block *block = (Block *) block_account->data;

    // Block must be correctly sized for the number of entries it contains
    if (block_account->data_len != compute_block_size(block->config.total_entry_count)) {
        return 0;
    }

    // If the block does not have the correct data type, then this is an error
    if (block->data_type != DataType_Block) {
        return 0;
    }

    return (Block *) block;
}


static bool is_block_complete(const Block *block)
{
    // Block is complete if the number of added entries equals the total number of entries
    return (block->added_entries_count == block->config.total_entry_count);
}


// This function assumes that the block is complete
static bool is_complete_block_revealable(const Block *block, const Clock *clock)
{
    // If all mysteries have been sold, then the block is revealable
    if (block->mysteries_sold_count == block->config.total_mystery_count) {
        return true;
    }

    // Otherwise, it's revealable if the mystery phase has passed (even though not all mysteries were sold)
    timestamp_t mystery_phase_end = block->block_start_timestamp + block->config.mystery_phase_duration;
    return (clock->unix_timestamp > mystery_phase_end);
}
