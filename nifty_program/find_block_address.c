

static bool find_block_address(uint8_t *seed, uint16_t seed_len, SolPubkey *return_key)
{
    // One seed: the group id and the block id
    SolSignerSeed sol_seed = { seed, seed_len };
    
    SolPubkey nifty_stakes_program_id = NIFTY_STAKES_PROGRAM_ID_BYTES;
    
    // Don't care about the resulting bump seed; the client must have already computed it and supplied the correct
    // address as block_account, otherwise this transaction will fail because the incorrect block_account will be
    // listed in accounts and will be rejected
    uint8_t junk;

    return !sol_try_find_program_address(&sol_seed, 1, &nifty_stakes_program_id, return_key, &junk);
}
