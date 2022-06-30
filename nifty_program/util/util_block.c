#pragma once

#include "inc/block.h"
#include "util/util_accounts.c"


// Given a block account, returns the validated Block or null if the block account is invalid in some way.
static Block *get_validated_block(SolAccountInfo *block_account)
{
    // Block must have at least enough data in it to store a block
    if (block_account->data_len < sizeof(Block)) {
        return 0;
    }
        
    Block *block = (Block *) block_account->data;

    // If the block does not have the correct data type, then this is an error
    if (block->data_type != DataType_Block) {
        return 0;
    }

    // Make sure that the block account is owned by the nifty stakes program
    if (!is_nifty_program(block_account->owner)) {
        return 0;
    }

    return block;
}


static bool is_block_complete(const Block *block)
{
    // Block is complete if the number of added entries equals the total number of entries
    return (block->state.added_entries_count == block->config.total_entry_count);
}


// This function assumes that the block is complete
static bool is_complete_block_revealable(const Block *block, const Clock *clock)
{
    // If all mysteries have been sold, then the block is revealable
    if (block->state.mysteries_sold_count == block->config.total_mystery_count) {
        return true;
    }
    
    // Otherwise, it's revealable if the mystery phase has passed (even though not all mysteries were sold)
    timestamp_t mystery_phase_end = block->state.block_start_timestamp + block->config.mystery_phase_duration;
    return (clock->unix_timestamp > mystery_phase_end);
}


#if 0
// This function assumes that the block is complete
static bool is_complete_block_in_reveal_grace_period(const Block *block, const Clock *clock)
{
    
}
#endif
