

uint64_t compute_block_size(uint16_t mint_data_size, uint16_t total_entry_count)
{
    // Use the BLOCK_ENTRY macro to compute how much data will be needed.  This macro needs only a BlockData with a
    // valid mint_data_size set to compute an index.
    BlockData bd;
    bd.config.mint_data_size = mint_data_size;

    // The total space needed is from the beginning of a BlockData to the entries element one beyond the total
    // supported (i.e. if there are 100 entries, then then entry at index 100 starts at the first byte beyond the
    // array)
    return ((uint64_t) BLOCK_ENTRY(&bd, total_entry_count)) - ((uint64_t) &bd);
}
