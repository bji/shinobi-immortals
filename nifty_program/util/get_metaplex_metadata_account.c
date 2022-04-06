
static bool get_metaplex_metadata_account(SolPubkey *mint, SolPubkey *fill_in)
{
    // Metaplex uses ['metadata', metadata_program_id, mint_id] as seeds
    SolSignerSeed seeds[3];
    seeds[0].addr = (const uint8_t *) "metadata";
    seeds[0].len = 8;
    SolPubkey metaplex_program_id = METAPLEX_METADATA_PROGRAM_ID_BYTES;
    seeds[1].addr = (const uint8_t *) &metaplex_program_id;
    seeds[1].len = sizeof(SolPubkey);
    seeds[2].addr = (const uint8_t *) mint;
    seeds[2].len = sizeof(SolPubkey);
    SolPubkey found_metadata_address;
    uint8_t found_bump_seed;

    return !sol_try_find_program_address(seeds, 3, &metaplex_program_id, fill_in, &found_bump_seed);
}
