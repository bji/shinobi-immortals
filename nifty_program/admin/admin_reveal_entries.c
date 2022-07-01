#pragma once

#include "admin/reveal_single_entry.c"


// Account references:
// 0. `[]` Program config account
// 1. `[SIGNER]` -- This must be the admin account
// 2. `[WRITE]` -- The block account address
// 3. `[]` -- nifty authority
// 4. `[]` -- Metaplex metadata program id (for cross-program invoke)
// 5. `[]` -- Token mint account
// 6. `[]` -- Token account
// 7. `[]` -- Metaplex metadata account
// 8. `[WRITE]` -- Entry account
// (Repeat 5-8 for each additional entry in the transaction)

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
    return (5 + (4 * entry_index));
}


static uint8_t index_of_reveal_token_account(uint8_t entry_index)
{
    return (6 + (4 * entry_index));
}


static uint8_t index_of_reveal_metaplex_metadata_account(uint8_t entry_index)
{
    return (7 + (4 * entry_index));
}


static uint8_t index_of_reveal_entry_account(uint8_t entry_index)
{
    return (8 + (4 * entry_index));
}


static uint64_t admin_reveal_entries(SolParameters *params)
{
    // Sanitize the accounts.  There must be at least 4
    if (params->ka_num < 4) {
        return Error_IncorrectNumberOfAccounts;
    }

    SolAccountInfo *config_account = &(params->ka[0]);
    SolAccountInfo *admin_account = &(params->ka[1]);
    SolAccountInfo *block_account = &(params->ka[2]);
    SolAccountInfo *authority_account = &(params->ka[3]);
    // Account index 4 must be the metaplex metadata program account; it is not checked here because if it's not the
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

    // Make sure that the number of accounts in the instruction account list is 5 + (4 * the number of entries in
    // the transaction), because each NFT account to reveal in sequence has four corresponding accounts from the
    // instruction accounts array: 1. Mint account, 2. Token account, 3. Metaplex metadata account, 4. Entry account
    if (params->ka_num != (5 + (4 * data->entry_count))) {
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
            return Error_InvalidAccount_First + 6;
        }


        // Do a single reveal of this entry
        uint64_t result = reveal_single_entry(block, entry, &clock, salt, admin_account, mint_account,
                                              token_account, metaplex_metadata_account, params->ka,
                                              params->ka_num);

        // If that reveal failed, then the entire transaction fails
        if (result) {
            return result;
        }
    }

    // All entries revealed successfully
    return 0;
}
