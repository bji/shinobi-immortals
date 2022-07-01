#pragma once

#include "util/util_token.c"


// This function reveals a single entry, which means ensuring that it meets all requirements for being a valid
// reveal, and then updates the entry state to their post-reveal values.  It returns nonzero on error, zero on
// success.

static uint64_t reveal_single_entry(Block *block,
                                    Entry *entry,
                                    Clock *clock,
                                    salt_t salt,
                                    SolAccountInfo *admin_account,
                                    SolAccountInfo *mint_account,
                                    SolAccountInfo *token_account,
                                    SolAccountInfo *metaplex_metadata_account,
                                    SolAccountInfo *transaction_accounts,
                                    int transaction_accounts_len)                                    
{
    // Ensure that the mint is the correct mint account for this Entry
    if (!SolPubkey_same(mint_account->key, &(entry->mint_account.address))) {
        return Error_InvalidNFTAccount;
    }

    SolanaMintAccountData *mint_data = (SolanaMintAccountData *) mint_account->data;

    // Ensure that the token account is the correct token account for this Entry
    if (!SolPubkey_same(token_account->key, &(entry->token_account.address))) {
        return Error_InvalidNFTAccount;
    }

    SolanaTokenProgramTokenData *token_data = (SolanaTokenProgramTokenData *) token_account->data;

    // Ensure that the metaplex metadata account passed in is the actual metaplex metadata account for this token
    if (!SolPubkey_same(metaplex_metadata_account->key, &(entry->metaplex_metadata_account.address))) {
        return Error_InvalidNFTAccount;
    }

    // Ensure that the entry state is waiting for reveal, which is the only state in which reveal is allowed
    switch (get_entry_state(block, entry, clock)) {
    case EntryState_WaitingForRevealUnowned:
        // When an entry is revealed from an unowned state, it enters auction
        entry->auction.auction_begin_timestamp = clock->unix_timestamp;
        break;
    case EntryState_WaitingForRevealOwned:
        // In this case, the SOL that was originally paid by the purchaser was
        // moved into the authority account as a form of escrow, in case the
        // reveal did not happen, the user could request a refund after the
        // reveal period completed.  If a refund was not completed, then move
        // the funds from the escrow to the admin account
        if (!entry->refund_awarded) {
            uint64_t ret = util_transfer_lamports(&(Constants.nifty_authority_account), admin_account,
                                                  entry->purchase_price_lamports, transaction_accounts,
                                                  transaction_accounts_len);
            if (!ret) {
                return ret;
            }
        }
        break;
    default:
        return Error_EntryNotRevealable;
    }

    // OK at this point, it is known that the mint account is valid, the token account is valid, the metaplex metadata
    // accounts is valid, and the entry is in a valid state.  So now compute the SHA-256 hash that will ensure that
    // these are correct for the reveal of this entry.
    sha256_t computed_sha256;
    
    SolBytes bytes;
        
    // The computation of the hash is a two step process:
    // 1. Compute the individual SHA-256 hashe of the entry metadata, into a contiguous buffer of bytes.  Then append
    //    the 8 bytes of salt onto the end.
    // 2. Compute SHA-256 of this buffer
    {
        uint8_t buffer[sizeof(sha256_t) + 8];
        {
            SolBytes bytes;
            bytes.addr = (uint8_t *) &(entry->metadata);
            bytes.len = sizeof(entry->metadata);
            if (sol_sha256(&bytes, 1, buffer)) {
                return Error_InvalidHash;
            }
        }

        * (salt_t *) &(((sha256_t *) buffer)[1]) = salt;

        {
            SolBytes bytes;
            bytes.addr = buffer;
            bytes.len = sizeof(buffer);
            if (sol_sha256(&bytes, 1, (uint8_t *) &computed_sha256)) {
                return Error_InvalidHash;
            }
        }
    }

    // Now ensure that the entry's reveal_sha256 hash matches the computed hash.  This then verifies that the entry
    // has had its metadata set already to the correct revealed values.
    if (sol_memcmp(&computed_sha256, &(entry->reveal_sha256), sizeof(sha256_t))) {
        return Error_InvalidHash;
    }
    
    // Update the metaplex metadata for the NFT to include the level 0 state.
    uint64_t ret = set_metaplex_metadata_for_level(entry, 0, metaplex_metadata_account, transaction_accounts,
                                                   transaction_accounts_len);
    if (ret) {
        return ret;
    }

    // Now that the entry is revealed, its reveal_sha256 is zeroed out, which is what puts the Entry into its
    // revealed state.
    sol_memset(&(entry->reveal_sha256), 0, sizeof(entry->reveal_sha256));

    // That's all that is needed to complete the reveal of an entry
    return 0;
}
