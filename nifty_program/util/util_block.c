
#ifndef UTIL_BLOCK_C
#define UTIL_BLOCK_C

#include "inc/block.h"


static bool is_block_complete(const Block *block)
{
    // Block is complete if the number of added entries equals the total number of entries
    return (block->state.added_entries_count == block->config.total_entry_count);
}


#endif // UTIL_BLOCK_C
