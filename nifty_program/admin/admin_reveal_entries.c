#pragma once

#include "util/util_token.c"
#include "util/util_transfer_lamports.c"


// Account references:
// 0. `[]` Program config account
// 1. `[SIGNER]` -- This must be the admin account
// 2. `[WRITE]` -- The block account address
// 3. `[WRITE]` -- nifty authority
// 4. `[]` -- System program id (for cross-program invoke)
// 5. `[]` -- Metaplex metadata program id (for cross-program invoke)
// 6. `[]` -- Token mint account
// 7. `[]` -- Token account
// 8. `[]` -- Metaplex metadata account
// 9. `[WRITE]` -- Entry account
// (Repeat 6-9 for each additional entry in the transaction)

typedef struct
{
    // This is the instruction code for RevealEntriesData
    uint8_t instruction_code;

    // Index within the block account of the first entry included here
    uint16_t first_entry;

    // Total number of entries in the entries array.  This is a u8 because each entry must have four corresponding
    // accounts in the instruction accounts array, and the entrypoint limits account number to 32, so 8 bits is enough
    uint8_t entry_count;

    // These are the salt values that were used to compute the SHA-256 hash of each entry
    salt_t entry_salt[0];
    
} RevealEntriesData;


// Forward declaration
static uint64_t reveal_single_entry(Block *block,
                                    Entry *entry,
                                    Clock *clock,
                                    salt_t salt,
                                    SolAccountInfo *admin_account,
                                    SolAccountInfo *authority_account,
                                    SolAccountInfo *mint_account,
                                    SolAccountInfo *token_account,
                                    SolAccountInfo *metaplex_metadata_account,
                                    SolAccountInfo *transaction_accounts,
                                    int transaction_accounts_len,
                                    /* modifies */ uint64_t *total_lamports_to_move);


static uint64_t compute_reveal_entries_data_size(uint16_t entry_count)
{
    RevealEntriesData *d = 0;

    // The total space needed is from the beginning of RevealEntriesData to the entries element one beyond the
    // total supported (i.e. if there are 100 entries, then then entry at index 100 starts at the first byte beyond
    // the array)
    return ((uint64_t) &(d->entry_salt[entry_count]));
}

static uint8_t index_of_reveal_mint_account(uint8_t entry_index)
{
    return (6 + (4 * entry_index));
}


static uint8_t index_of_reveal_token_account(uint8_t entry_index)
{
    return (7 + (4 * entry_index));
}


static uint8_t index_of_reveal_metaplex_metadata_account(uint8_t entry_index)
{
    return (8 + (4 * entry_index));
}


static uint8_t index_of_reveal_entry_account(uint8_t entry_index)
{
    return (9 + (4 * entry_index));
}


static uint64_t admin_reveal_entries(SolParameters *params)
{
    // Sanitize the accounts.  There must be at least 10.
    if (params->ka_num < 10) {
        return Error_IncorrectNumberOfAccounts;
    }

    SolAccountInfo *config_account = &(params->ka[0]);
    SolAccountInfo *admin_account = &(params->ka[1]);
    SolAccountInfo *block_account = &(params->ka[2]);
    SolAccountInfo *authority_account = &(params->ka[3]);
    // Account index 4 must be the system program account; it is not checked here because if it's not the
    // correct account, then the cross-program invoke below in reveal_single_entry will just fail the transaction
    // Account index 5 must be the metaplex metadata program account; it is not checked here because if it's not the
    // correct account, then the cross-program invoke below in reveal_single_entry will just fail the transaction

    // Ensure the the transaction has been authenticated by the admin
    if (!is_admin_authenticated(config_account, admin_account)) {
        return Error_PermissionDenied;
    }

    // Ensure that the block account is writable
    if (!block_account->is_writable) {
        return Error_BadPermissions;
    }

    // Check the authority account to make sure it's the right one
    if (!is_nifty_authority_account(authority_account->key)) {
        return Error_InvalidAccount_First + 3;
    }

    // Data can be used now
    RevealEntriesData *data = (RevealEntriesData *) params->data;

    // Make sure that the data is properly sized given the number of entries
    if (params->data_len != compute_reveal_entries_data_size(data->entry_count)) {
        return Error_InvalidDataSize;
    }

    // Make sure that the number of accounts in the instruction account list is 6 + (4 * the number of entries in
    // the transaction), because each NFT account to reveal in sequence has four corresponding accounts from the
    // instruction accounts array: 1. Mint account, 2. Token account, 3. Metaplex metadata account, 4. Entry account
    if (params->ka_num != (6 + (4 * data->entry_count))) {
        return Error_InvalidData_First;
    }

    // Get the valid block data
    Block *block = get_validated_block(block_account);
    if (!block) {
        return Error_InvalidAccount_First + 2;
    }

    // Make sure that the last entry to reveal does not exceed the number of entries in the block
    if ((data->first_entry + data->entry_count) > block->config.total_entry_count) {
        return Error_InvalidData_First + 1;
    }

    // Ensure that the block is complete; cannot reveal entries of a block that is not complete yet
    if (!is_block_complete(block)) {
        return Error_BlockNotComplete;
    }
    
    // Load the clock, which is needed by reveals
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }
        
    // Ensure that the block has reached its reveal criteria; cannot reveal entries of a block that has not reached
    // reveal
    if (!is_complete_block_revealable(block, &clock)) {
        return Error_BlockNotRevealable;
    }

    // Keep track of total number of escrow lamports, paid to the authority account by purchasers of "mystery"
    // un-revealed NFTs, that need to be moved to the admin account now that the entries are revealed.
    uint64_t total_lamports_to_move = 0;
    
    // Reveal entries one by one
    for (uint16_t i = 0; i < data->entry_count; i++) {
        uint16_t destination_index = data->first_entry + i;

        // This is the account info of the NFT mint, as passed into the accounts list
        SolAccountInfo *mint_account = &(params->ka[index_of_reveal_mint_account(i)]);
        
        // This is the token account of the NFT, as passed into the accounts list
        SolAccountInfo *token_account = &(params->ka[index_of_reveal_token_account(i)]);
        
        // This is the account info of the metaplex metadata for the NFT, as passed into the accounts list
        SolAccountInfo *metaplex_metadata_account = &(params->ka[index_of_reveal_metaplex_metadata_account(i)]);

        // This is the account info of the entry, as passed into the accounts list
        SolAccountInfo *entry_account = &(params->ka[index_of_reveal_entry_account(i)]);
        
        // The salt is the corresponding entry in the input data
        salt_t salt = data->entry_salt[i];

        // Get the validated Entry
        Entry *entry = get_validated_entry(block, destination_index, entry_account);
        if (!entry) {
            return Error_InvalidAccount_First + 9;
        }


        // Do a single reveal of this entry
        uint64_t result = reveal_single_entry(block, entry, &clock, salt, admin_account, authority_account,
                                              mint_account, token_account, metaplex_metadata_account, params->ka,
                                              params->ka_num, /* modifies */ &total_lamports_to_move);

        // If that reveal failed, then the entire transaction fails
        if (result) {
            return result;
        }
    }

    // All entries revealed successfully.  Move the escrow lamports that needed to move.  This must be done at the end
    // to avoid errors with modifies accounts used in cross-program invoke elsewhere in the transaction execution.
    if (total_lamports_to_move) {
        // Admin account and authority accounts must be writable
        if (!admin_account->is_writable) {
            return Error_InvalidAccountPermissions_First + 1;
        }
        if (!authority_account->is_writable) {
            return Error_InvalidAccountPermissions_First + 3;
        }
        *(admin_account->lamports) += total_lamports_to_move;
        *(authority_account->lamports) -= total_lamports_to_move;
    }
    
    return 0;
}


// This function reveals a single entry, which means ensuring that it meets all requirements for being a valid
// reveal, and then updates the entry state to their post-reveal values.  It returns nonzero on error, zero on
// success.

static uint64_t reveal_single_entry(Block *block,
                                    Entry *entry,
                                    Clock *clock,
                                    salt_t salt,
                                    SolAccountInfo *admin_account,
                                    SolAccountInfo *authority_account,
                                    SolAccountInfo *mint_account,
                                    SolAccountInfo *token_account,
                                    SolAccountInfo *metaplex_metadata_account,
                                    SolAccountInfo *transaction_accounts,
                                    int transaction_accounts_len,
                                    /* modifies */ uint64_t *total_lamports_to_move)
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
        entry->auction.begin_timestamp = clock->unix_timestamp;
        break;
    case EntryState_WaitingForRevealOwned:
        // The SOL that was originally paid by the purchaser was moved into the authority account as a form of escrow,
        // in case the reveal did not happen, the user could request a refund after the reveal period completed.  If a
        // refund was not completed, then move the funds from the escrow to the admin account.  The lamports to move
        // is added to the total value that is accumulated during the tx, to be moved at the very end.
        if (!entry->refund_awarded) {
            *total_lamports_to_move += entry->purchase_price_lamports;
        }
        break;
    default:
        return Error_EntryNotRevealable;
    }

    // OK at this point, it is known that the mint account is valid, the token account is valid, the metaplex metadata
    // accounts is valid, and the entry is in a valid state.  So now compute the SHA-256 hash that will ensure that
    // these are correct for the reveal of this entry.
    sha256_t computed_sha256;
    
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
