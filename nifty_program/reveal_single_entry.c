

// This function reveals a single entry, which means ensuring that it meets all requirements for being a valid
// reveal, and then updates the entry state to their post-reveal values.  It returns nonzero on error, zero on
// success.

static uint64_t reveal_single_entry(BlockEntry *entry,
                                    Clock *clock,
                                    uint64_t salt,
                                    SolAccountInfo *mint_account_info,
                                    SolAccountInfo *token_account_info,
                                    SolAccountInfo *metaplex_metadata_account_info,
                                    SolAccountInfo *shinobi_metadata_account_info)
{
    // Check to make sure that the entry state is pre-reveal (can't reveal after it's already been revealed!)
    if (entry->state != BlockEntryState_PreReveal) {
        return Error_AlreadyRevealed;
    }

    // Ensure that the NFT is owned by this program; if it is not, then it is not a valid NFT for reveal, because the
    // nifty program must own all NFTs in each block, so that they can be traded for tickets and stake accounts etc.
    if (!is_nifty_stakes_program(token_account_info->owner)) {
        return Error_InvalidNFTOwner;
    }

    // Ensure that the token's mint is the mint actually passed in as mint_account_info.  For solana tokens,
    // the mint is the SolPubkey value starting at offset 4 of the account data.
    // There must be enough data in the account to actually do this comparison
    if ((token_account_info->data_len < (4 + sizeof(SolPubkey))) ||
        !SolPubkey_same((SolPubkey *) &(token_account_info->data[4]), mint_account_info->key)) {
        return Error_InvalidNFTAccount;
    }

    // Ensure that the mint is owned by the Solana token program, which makes it a solana token program token
    if (!is_solana_token_program(mint_account_info->owner)) {
        return Error_InvalidNFTAccount;
    }

    // Ensure that the metaplex metadata account is actually owned by the metaplex metadata program
    if (!is_metaplex_metadata_program(metaplex_metadata_account_info->owner)) {
        return Error_InvalidNFTAccount;
    }
                                          
    // Ensure that the metaplex metadata account passed in is the actual metaplex metadata account for this token
    {
        // Metaplex uses ['metadata', metadata_program_id, mint_id] as seeds
        SolSignerSeed seeds[3];
        seeds[0].addr = (const uint8_t *) "metadata";
        seeds[0].len = 8;
        SolPubkey metaplex_program_id = METAPLEX_METADATA_PROGRAM_ID_BYTES;
        seeds[1].addr = (const uint8_t *) &metaplex_program_id;
        seeds[1].len = sizeof(SolPubkey);
        seeds[2].addr = (const uint8_t *) mint_account_info->key;
        seeds[2].len = sizeof(SolPubkey);
        SolPubkey found_metadata_address;
        uint8_t found_bump_seed;
        if (sol_try_find_program_address(seeds, 3, &metaplex_program_id, &found_metadata_address, &found_bump_seed)) {
            return Error_InvalidNFTAccount;
        }
        if (!SolPubkey_same(metaplex_metadata_account_info->key, &found_metadata_address)) {
            return Error_InvalidNFTAccount;
        }
    }

    // Ensure that the shinobi metadata account is actually owned by the shinobi metadata program
    if (!is_shinobi_metadata_program(shinobi_metadata_account_info->owner)) {
        return Error_InvalidNFTAccount;
    }
    
    // Ensure that the shinobi metadata account passed in is the actual shinobi metadata account for this token
    {
        // Shinobi uses [mint_id] as the seed
        SolSignerSeed seeds[1];
        SolPubkey shinobi_program_id = SHINOBI_METADATA_PROGRAM_ID_BYTES;
        seeds[0].addr = (const uint8_t *) mint_account_info->key;
        seeds[0].len = sizeof(SolPubkey);
        SolPubkey found_metadata_address;
        uint8_t found_bump_seed;
        if (sol_try_find_program_address(seeds, 1, &shinobi_program_id, &found_metadata_address, &found_bump_seed)) {
            return Error_InvalidNFTAccount;
        }
        if (!SolPubkey_same(shinobi_metadata_account_info->key, &found_metadata_address)) {
            return Error_InvalidNFTAccount;
        }
    }
    
    // OK at this point, it is known that the token account is valid, the mint account is valid, and the metadata
    // accounts are both valid.  So now compute the SHA-256 hash that will ensure that these are correct for this
    // entry.
    // The computation of the hash is a two step process:
    // 1. Compute the individual SHA-256 hashes of the token account address, metaplex metadata, and shinobi metadata,
    //    into a contiguous buffer of 32 + 32 + 32 bytes.  Then append the 8 bytes of salt onto the end.
    // 2. Compute SHA-256 of this 32 + 32 + 32 + 8 byte buffer
    uint8_t buffer[32 + 32 + 32 + 8];

    {
        SolBytes bytes;
        bytes.addr = (uint8_t *) (token_account_info->key);
        bytes.len = sizeof(SolPubkey);
        if (sol_sha256(&bytes, 1, (uint8_t *) &(((sha256_t *) buffer)[0]))) {
            return Error_InvalidHash;
        }
    }
    
    {
        SolBytes bytes;
        bytes.addr = (uint8_t *) (metaplex_metadata_account_info->data);
        bytes.len = metaplex_metadata_account_info->data_len;
        if (sol_sha256(&bytes, 1, (uint8_t *) &(((sha256_t *) buffer)[1]))) {
            return Error_InvalidHash;
        }
    }

    {
        SolBytes bytes;
        bytes.addr = (uint8_t *) (shinobi_metadata_account_info->data);
        bytes.len = shinobi_metadata_account_info->data_len;
        if (sol_sha256(&bytes, 1, (uint8_t *) &(((sha256_t *) buffer)[2]))) {
            return Error_InvalidHash;
        }
    }

    * ((uint64_t *) &(buffer[32 + 32 + 32])) = salt;
    
    sha256_t computed_sha256;
    
    {
        SolBytes bytes;
        bytes.addr = buffer;
        bytes.len = sizeof(buffer);
        if (sol_sha256(&bytes, 1, (uint8_t *) &computed_sha256)) {
            return Error_InvalidHash;
        }
    }

    // Now ensure that the entry's sha256 hash matches the computed hash.  This then verifies that this is the
    // corrent NFT for this entry.
    if (sol_memcmp(&computed_sha256, &(entry->entry_sha256), sizeof(sha256_t))) {
        return Error_InvalidHash;
    }

    // Reveal can now proceed

    // Set the token account into the entry, replacing the sha256 that was there previously
    entry->token_account = *(token_account_info->key);

    // And update the entry state; if there was no ticket purchased (indicated by the ticket_or_bid_account value of
    // the entry being empty), then the entry is unsold.  Its current auction begin time is set.
    if (is_empty_pubkey(&(entry->ticket_or_bid_account))) {
        entry->unsold.auction_begin_timestamp = clock->unix_timestamp;
        entry->state = BlockEntryState_Unsold;
    }
    // Else there was a ticket purchased, so this entry is now awaiting ticket redeem
    else {
        entry->state = BlockEntryState_AwaitingTicketRedeem;
    }
    
    // That's all that is needed to complete the reveal of an entry for which a ticket was purchased
    return 0;
}
