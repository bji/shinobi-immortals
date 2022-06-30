#pragma once

#include "inc/block.h"
#include "inc/clock.h"
#include "inc/entry.h"
#include "util/util_accounts.c"
#include "util/util_authentication.c"
#include "util/util_block.c"
#include "util/util_entry.c"
#include "util/util_metaplex.c"
#include "util/util_token.c"


// Account references:
// 0. `[]` Program config account
// 1. `[SIGNER]` -- This must be the admin account
// 2. `[WRITE]` -- The funding account
// 3. `[WRITE]` -- The block account
// 4. `[]` -- The nifty authority account
// 5. `[]` -- The system program
// 6. `[]` -- The SPL token program
// 7. `[]` -- The metaplex metadata program
// 8. `[]` -- The rent sysvar
// The following are repeated for each entry:
// a. `[WRITE]` -- Entry account
// b. `[WRITE]` -- Mint account
// c. `[WRITE]` -- Token account
// d. `[WRITE]` -- Metaplex metadata account


// Details provided along with each entry to be added
typedef struct
{
    // The bump seed that is used to generate the entry account for the entry
    uint8_t entry_bump_seed;

    // The bump seed that is used to generate the mint account for the entry
    uint8_t mint_bump_seed;

    // The bump seed that is used to generate the token account for the entry
    uint8_t token_bump_seed;
    
    // The bump seed that is used to generate the metaplex metadata account for the entry
    uint8_t metaplex_metadata_bump_seed;
    
    // The sha256 of the metadata + salt to copy into the entry as reveal_sha256, which will ensure that when the
    // reveal of the entry occurs, it will reveal metadata which was already determined at the time that the entry was
    // added
    sha256_t sha256;
    
} EntryDetails;


typedef struct
{
    // This is the instruction code for AddEntriesToBlockData
    uint8_t instruction_code;

    // This is the initial uri to use as the uri member of the metaplex metadata
    uint8_t metaplex_metadata_uri[200];

    // These are the creator pubkeys to add to the metaplex metadata.  Up to 2, and if either is all zeroes,
    // then it is not used
    SolPubkey metaplex_metadata_creator_1;
    SolPubkey metaplex_metadata_creator_2;

    // Index of first entry included here
    uint16_t first_entry;

    // Each account to be computed requires a bump seed, and those bump seeds are provided in groups of 4 per
    // entry, with the mint account bump seed first, metaplex metadata account bump seed second, metaplex edition
    // metadata account bump seed third, and entry account bump seed fourth
    EntryDetails entry_details[0];

} AddEntriesToBlockData;


// Forward declaration
static uint64_t add_entry(Block *block, uint16_t entry_index, SolPubkey *authority_key,
                          SolPubkey *funding_key, SolAccountInfo *entry_accounts, AddEntriesToBlockData *data,
                          EntryDetails *entry_details, SolAccountInfo *transaction_accounts,
                          int transaction_accounts_len);


static uint64_t compute_add_entries_data_size(uint16_t entry_count)
{
    AddEntriesToBlockData *d = 0;

    // The total space needed is from the beginning of AddEntriesToBlockData to the entries element one beyond the
    // total supported (i.e. if there are 100 entries, then then entry at index 100 starts at the first byte beyond
    // the array)
    return ((uint64_t) &(d->entry_details[entry_count]));
}

    
static uint64_t admin_add_entries_to_block(SolParameters *params)
{
    // Make sure that at least the non-entry accounts are present
    if (params->ka_num < 9) {
        return Error_IncorrectNumberOfAccounts;
    }

    // The total number of entry accounts is everything after the non-entry accounts
    uint8_t entry_address_count = params->ka_num - 9;
    // The number of entry accounts must be a multiple of 4
    if (entry_address_count % 4) {
        return Error_IncorrectNumberOfAccounts;
    }

    // There are 4 accounts per entry
    uint8_t entry_count = entry_address_count / 4;

    // These are the non-entry accounts
    SolAccountInfo *config_account = &(params->ka[0]);
    SolAccountInfo *admin_account = &(params->ka[1]);
    SolAccountInfo *funding_account = &(params->ka[2]);
    SolAccountInfo *block_account = &(params->ka[3]);
    SolAccountInfo *authority_account = &(params->ka[4]);
    // The sixth account is the system program, but this does not need to be checked; if it is not provided, then the
    // system cross program invocations will fail and the transaction will fail
    // The seventh account is the SPL token program, but this does not need to be checked; if it is not
    // provided, then the SPL token cross program invocations will fail and the transaction will fail
    // The eigth account is the metaplex metadata program, but this does not need to be checked; if it is not
    // provided, then the metaplex cross program invocations will fail and the transaction will fail
    // The ninth account is the rent sysvar, but this does not need to be checked; if it is not provided, then the
    // metaplex cross program invocations will fail and the transaction will fail
    
    // Ensure the the transaction has been authenticated by the admin
    if (!is_admin_authenticated(config_account, admin_account)) {
        return Error_PermissionDenied;
    }

    // Ensure that the block account is writable
    if (!block_account->is_writable) {
        return Error_BadPermissions;
    }

    // Ensure that the block account is owned by the program
    if (!is_nifty_program(block_account->owner)) {
        return Error_InvalidAccount_First + 2;
    }

    // Ensure that the authority account as passed in is the correct account
    if (!is_nifty_authority_account(authority_account->key)) {
        return Error_InvalidAccount_First + 3;
    }

    // Ensure that the instruction data has the minimum possible size
    if (params->data_len < sizeof(AddEntriesToBlockData)) {
        return Error_InvalidDataSize;
    }

    // Data can be used now
    AddEntriesToBlockData *data = (AddEntriesToBlockData *) params->data;

    // Make sure that the data is properly sized given the number of entries
    if (params->data_len != compute_add_entries_data_size(entry_count)) {
        return Error_InvalidDataSize;
    }

    // Here is the existing block data
    Block *block = (Block *) block_account->data;

    // If the block does not have the correct data type, then this is an error (admin accidentally treating a bid as a
    // block?)
    if (block->data_type != DataType_Block) {
        return Error_IncorrectAccountType;
    }
    
    // Fail if the block is already complete
    if (is_block_complete(block)) {
        return Error_BlockAlreadyComplete;
    }
    
    // Make sure that the first entry supplied is the next entry to be added to the block
    if (data->first_entry != block->state.added_entries_count) {
        return Error_InvalidData_First;
    }

    // Make sure that the total number of entries to add does not exceed the number of entries in the block.
    if (entry_count > (block->config.total_entry_count - block->state.added_entries_count)) {
        return Error_InvalidData_First + 1;
    }

    // Add each entry one by one
    for (uint8_t i = 0; i < entry_count; i++) {
        // This is the group of accounts for this entry
        SolAccountInfo *entry_accounts = &(params->ka[9 + (4 * i)]);

        // This is the entry details for the entry
        EntryDetails *entry_details = &(data->entry_details[i]);

        uint16_t entry_index = data->first_entry + i;

        // Save the entry bump seed in the block
        block->entry_bump_seeds[entry_index] = entry_details->entry_bump_seed;

        // Add the entry
        uint64_t result = add_entry(block, entry_index, authority_account->key, funding_account->key,
                                    entry_accounts, data, entry_details, params->ka, params->ka_num);
        
        if (result) {
            return result;
        }
    }

    // Now the total number of entries added is increased
    block->state.added_entries_count += entry_count;

    // If the block has just been completed, then set the block_start_time to the current time.
    if (is_block_complete(block)) {
        Clock clock;
        if (sol_get_clock_sysvar(&clock)) {
            return Error_FailedToGetClock;
        }
        block->state.block_start_timestamp = clock.unix_timestamp;
        // If the number of mysteries is 0, then the mystery phase is already done, and so the reveal phase starts
        // immediately.
        if (block->config.total_mystery_count == 0) {
            block->state.reveal_period_start_timestamp = clock.unix_timestamp;
        }
    }
    
    return 0;
}


static uint64_t add_entry(Block *block, uint16_t entry_index, SolPubkey *authority_key,
                          SolPubkey *funding_key, SolAccountInfo *entry_accounts, AddEntriesToBlockData *data,
                          EntryDetails *entry_details, SolAccountInfo *transaction_accounts,
                          int transaction_accounts_len)
{
    SolAccountInfo *entry_account =                     &(entry_accounts[0]);
    SolAccountInfo *mint_account =                      &(entry_accounts[1]);
    SolAccountInfo *token_account =                     &(entry_accounts[2]);
    SolAccountInfo *metaplex_metadata_account =         &(entry_accounts[3]);

    // Faster fail: make sure all accounts are writable
    if (!entry_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 11;
    }
    if (!mint_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 12;
    }
    if (!token_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 13;
    }
    if (!metaplex_metadata_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 14;
    }

    // Compute entry address, ensure that it is as supplied
    {
        SolPubkey entry_address;
        if (!compute_entry_address(block->config.group_number, block->config.block_number, entry_index,
                                   entry_details->entry_bump_seed, &entry_address)) {
            return Error_InvalidAccount_First + 11;
        }
        if (!SolPubkey_same(&entry_address, entry_account->key)) {
            return Error_InvalidAccount_First + 11;
        }
    }

    // Compute mint address, ensure that it is as supplied
    {
        SolPubkey mint_address;
        if (!compute_mint_address(block->config.group_number, block->config.block_number, entry_index,
                                  entry_details->mint_bump_seed, &mint_address)) {
            return Error_InvalidAccount_First + 12;
        }
        if (!SolPubkey_same(&mint_address, mint_account->key)) {
            return Error_InvalidAccount_First + 12;
        }
    }

    // Compute token address, ensure that it is as supplied
    {
        SolPubkey token_address;
        if (!compute_token_address(block->config.group_number, block->config.block_number, entry_index,
                                   entry_details->token_bump_seed, &token_address)) {
            return Error_InvalidAccount_First + 13;
        }
        if (!SolPubkey_same(&token_address, token_account->key)) {
            return Error_InvalidAccount_First + 13;
        }
    }

    // Compute metaplex metadata address, ensure that it is as supplied
    {
        SolPubkey metaplex_metadata_address;
        if (!compute_metaplex_metadata_address(mint_account->key, entry_details->metaplex_metadata_bump_seed,
                                               &metaplex_metadata_address)) {
            return Error_InvalidAccount_First + 14;
        }
        if (!SolPubkey_same(&metaplex_metadata_address, metaplex_metadata_account->key)) {
            return Error_InvalidAccount_First + 14;
        }
    }

    // Create the mint
    uint64_t result = create_token_mint(mint_account->key, entry_details->mint_bump_seed, authority_key, funding_key,
                                        block->config.group_number, block->config.block_number, entry_index,
                                        transaction_accounts, transaction_accounts_len);

    if (result) {
        return result;
    }

    // Create the token account
    result = create_pda_token_account(token_account->key, entry_details->token_bump_seed, mint_account->key,
                                      funding_key, block->config.group_number, block->config.block_number, entry_index,
                                      transaction_accounts, transaction_accounts_len);
    
    if (result) {
        return result;
    }

    // Mint one instance of the token into the token account
    result = mint_token(mint_account->key, token_account->key, transaction_accounts, transaction_accounts_len);

    if (result) {
        return result;
    }
    
    // Create the metaplex metadata
    result = create_metaplex_metadata(metaplex_metadata_account->key, mint_account->key, authority_key, funding_key,
                                      block->config.group_number, block->config.block_number, entry_index,
                                      data->metaplex_metadata_uri, &(data->metaplex_metadata_creator_1),
                                      &(data->metaplex_metadata_creator_2), transaction_accounts,
                                      transaction_accounts_len);

    if (result) {
        return result;
    }
    
    // Revoke the mint authority so that it is truly an NFT
    result = revoke_mint_authority(mint_account->key, authority_key, transaction_accounts, transaction_accounts_len);
    
    if (result) {
        return result;
    }

    #if 0    

    // Create the metaplex edition metadata.  Or not:
    // 1. It's useless and an extra cost
    // 2. Metaplex currently requires that they be mint authority for any token which has a master edition.  But this
    //    then gives them rights to mint another token, which in effect gives them the ability to return that
    //    token for the original stake account, which basically gives metaplex ownership of all stake accounts.
    //
    // Edition metadata will only be possible of metaplex changes their program to not require mint authority (which
    // they don't need anyway, they just need to verify that there is no mint authority), and is only really needed
    // if it proves necessarry for people to see this useless "master edition" metadata.

    #endif

    // Create the entry.  Allow 128 bytes of room for "expansion metadata".
    uint64_t entry_size = sizeof(Entry) + 128;

    // Use the provided bump seed plus the deterministic seed parts to create the entry account address
    uint8_t prefix = PDA_Account_Seed_Prefix_Entry;
    SolSignerSeed seed_parts[] = { { &prefix, sizeof(prefix) },
                                   { (uint8_t *) &(block->config.group_number), sizeof(block->config.group_number) },
                                   { (uint8_t *) &(block->config.block_number), sizeof(block->config.block_number) },
                                   { (uint8_t *) &entry_index, sizeof(entry_index) },
                                   { &(entry_details->entry_bump_seed), 1 } };

    // Create the entry account
    if (create_pda(entry_account->key, seed_parts, sizeof(seed_parts) / sizeof(seed_parts[0]), funding_key,
                   &(Constants.nifty_program_id), get_rent_exempt_minimum(entry_size), entry_size, transaction_accounts,
                   transaction_accounts_len)) {
        return Error_CreateAccountFailed;
    }

    Entry *entry = (Entry *) (entry_account->data);

    entry->data_type = DataType_Entry;

    entry->entry_index = entry_index;

    entry->mint_account.address = *(mint_account->key);
    entry->mint_account.bump_seed = entry_details->mint_bump_seed;
    
    entry->token_account.address = *(token_account->key);
    entry->token_account.bump_seed = entry_details->token_bump_seed;
    
    entry->metaplex_metadata_account.address = *(metaplex_metadata_account->key);
    entry->metaplex_metadata_account.bump_seed = entry_details->metaplex_metadata_bump_seed;

    *(entry->reveal_sha256) = *(entry_details->sha256);

    // Ensure that the entry is not revealed already, because a newly added entry must be in a prereveal state
    if (is_entry_revealed(entry)) {
        return Error_AlreadyRevealed;
    }

    return 0;
}
