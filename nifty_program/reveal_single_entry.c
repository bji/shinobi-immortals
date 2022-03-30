

// This function reveals a single entry, which means ensuring that it meets all requirements for being a valid
// reveal, and then updates the entry state to their post-reveal values.  It returns nonzero on error, zero on
// success.

static uint64_t reveal_single_entry(Block *block,
                                    Clock *clock,
                                    salt_t salt,
                                    SolAccountInfo *mint_account_info,
                                    SolAccountInfo *token_account_info,
                                    SolAccountInfo *metaplex_metadata_account_info,
                                    SolAccountInfo *entry_account_info)
{
    // Ensure that the mint is owned by the Solana token program
    if (!is_solana_token_program(mint_account_info->owner)) {
        return Error_InvalidNFTAccount;
    }

    // Ensure that the mint is the correct size
    if (mint_account_info->data_len != sizeof(SolanaTokenProgramMintData)) {
        return Error_InvalidNFTAccount;
    }

    SolanaTokenProgramMintData *mint_data = (SolanaTokenProgramMintData *) mint_account_info->data;

    // Make sure that the mint is valid: must be initialized, with a supply of 1, no decimals, and no mint
    // authority or freeze authority
    if (!mint_data->is_initialized ||
        (mint_data->supply != 1) ||
        (mint_data->decimals != 0) ||
        (mint_data->has_mint_authority) ||
        (mint_data->has_freeze_authority)) {
        return Error_InvalidNFTAccount;
    }

    // Ensure that the token account is owned by the Solana token program
    if (!is_solana_token_program(token_account_info->owner)) {
        return Error_InvalidNFTAccount;
    }

    // Ensure that the token account is the correct size
    if (token_account_info->data_len != sizeof(SolanaTokenProgramTokenData)) {
        return Error_InvalidNFTAccount;
    }

    SolanaTokenProgramTokenData *token_data = (SolanaTokenProgramTokenData *) token_account_info->data;

    // Ensure that the token is is valid.  Must be minted by the mint, have supply of 1, be initialized, and
    // no other settings
    if (!SolPubkey_same((SolPubkey *) &(token_data->mint), mint_account_info->key) ||
        (token_data->amount != 1) ||
        token_data->has_delegate ||
        (token_data->account_state != SolanaTokenAccountState_Initialized) ||
        token_data->has_is_native ||
        token_data->has_close_authority) {
        return Error_InvalidNFTAccount;
    }

    // Ensure that the token is owned by the nifty program; if it is not, then it is not a valid NFT for reveal,
    // because the nifty program must own all NFTs in each block, so that they can be traded for tickets and stake
    // accounts etc.
    if (!is_nifty_stakes_program(&(token_data->owner))) {
        return Error_InvalidNFTOwner;
    }

    // Ensure that the metaplex metadata account is actually owned by the metaplex metadata program
    if (!is_metaplex_metadata_program(metaplex_metadata_account_info->owner)) {
        return Error_InvalidNFTAccount;
    }
                                          
    // Ensure that the metaplex metadata account passed in is the actual metaplex metadata account for this token
    {
        SolPubkey metaplex_metadata_account;
        if (!get_metaplex_metadata_account(mint_account_info->key, &metaplex_metadata_account)) {
            return Error_InvalidNFTAccount;
        }
        if (!SolPubkey_same(metaplex_metadata_account_info->key, &metaplex_metadata_account)) {
            return Error_InvalidNFTAccount;
        }
    }

    // Ensure that the entry is properly sized
    if (entry_account_info->data_len != sizeof(Entry)) {
        return Error_InvalidNFTAccount;
    }

    Entry *entry = (Entry *) entry_account_info->data;
    
    // Ensure that the entry state is pre-reveal (can't reveal after it's already been revealed!)
    if (entry->state != EntryState_PreReveal) {
        return Error_AlreadyRevealed;
    }

    // OK at this point, it is known that the mint account is valid, the token account is valid, the metaplex metadata
    // accounts is valid, and the entry is in a valid state.  So now compute the SHA-256 hash that will ensure that
    // these are correct for the reveal of this entry.
    sha256_t computed_sha256;
    
    // The computation of the hash is a two step process:
    // 1. Compute the individual SHA-256 hashes of the metaplex metadata and entry metadata,
    //    into a contiguous buffer of bytes.  Then append the 8 bytes of salt onto the end.
    // 2. Compute SHA-256 of this buffer
    {
        uint8_t buffer[(2 * sizeof(sha256_t)) + 8];

        {
            SolBytes bytes;
            bytes.addr = (uint8_t *) (metaplex_metadata_account_info->data);
            bytes.len = metaplex_metadata_account_info->data_len;
            if (sol_sha256(&bytes, 1, (uint8_t *) &(((sha256_t *) buffer)[0]))) {
                return Error_InvalidHash;
            }
        }
        
        {
            SolBytes bytes;
            bytes.addr = (uint8_t *) &(entry->metadata);
            bytes.len = sizeof(entry->metadata);
            if (sol_sha256(&bytes, 1, (uint8_t *) &(((sha256_t *) buffer)[1]))) {
                return Error_InvalidHash;
            }
        }

        * (salt_t *) &(((sha256_t *) buffer)[2]) = salt;
        
        {
            SolBytes bytes;
            bytes.addr = buffer;
            bytes.len = sizeof(buffer);
            if (sol_sha256(&bytes, 1, (uint8_t *) &computed_sha256)) {
                return Error_InvalidHash;
            }
        }
    }

    // Now ensure that the entry's sha256 hash matches the computed hash.  This then verifies that this is the
    // correct NFT for this entry.
    if (sol_memcmp(&computed_sha256, &(entry->sha256), sizeof(sha256_t))) {
        return Error_InvalidHash;
    }

    // Reveal can now proceed

    // Set the token mint into the entry, replacing the sha256 that was there previously
    entry->token_mint = *(mint_account_info->key);

    // And update the entry state; if there was no ticket purchased (indicated by the ticket_or_bid_mint value of
    // the entry being empty), then the entry is unsold.  Its current auction begin time is set.
    if (is_empty_pubkey(&(entry->ticket_or_bid_mint))) {
        entry->unsold.auction_begin_timestamp = clock->unix_timestamp;
        entry->state = EntryState_Unsold;
    }
    // Else there was a ticket purchased, so this entry is now awaiting ticket redeem
    else {
        entry->state = EntryState_AwaitingTicketRedeem;
    }
    
    // That's all that is needed to complete the reveal of an entry
    return 0;
}
