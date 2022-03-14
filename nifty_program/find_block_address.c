

static bool find_block_address(uint32_t group_id, uint32_t block_id, SolPubkey *return_key)
{
    // Two seeds: the group id and the block id
    SolSignerSeed seeds[2] =
        { // First is group id
            { (uint8_t *) &group_id, 4 },
            // Second is block id
            { (uint8_t *) &block_id, 4 }
        };
    
    SolPubkey nifty_stakes_program_id = NIFTY_STAKES_PROGRAM_ID_BYTES;
    
    // Don't care about the resulting bump seed; the client must have already computed it and supplied the correct
    // address as block_account, otherwise this transaction will fail because the incorrect block_account will be
    // listed in accounts and will be rejected
    uint8_t junk;

    return !sol_try_find_program_address(seeds, 2, &nifty_stakes_program_id, return_key, &junk);
}
