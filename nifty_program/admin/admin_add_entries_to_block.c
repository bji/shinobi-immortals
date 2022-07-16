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


// Details provided along with each entry to be added
typedef struct
{
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
static uint64_t add_entry(SolAccountInfo *entry_accounts, SolPubkey *block_key, Block *block, uint16_t entry_index,
                          SolPubkey *funding_key, AddEntriesToBlockData *data, EntryDetails *entry_details,
                          SolAccountInfo *transaction_accounts, int transaction_accounts_len);


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
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,   config_account,                ReadOnly,   NotSigner,  KnownAccount_ProgramConfig);
        DECLARE_ACCOUNT(1,   admin_account,                 ReadOnly,   Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,   funding_account,               ReadWrite,  Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(3,   block_account,                 ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(4,   authority_account,             ReadOnly,   NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(5,   system_program_account,        ReadOnly,   NotSigner,  KnownAccount_SystemProgram);
        DECLARE_ACCOUNT(6,   spl_token_program_account,     ReadOnly,   NotSigner,  KnownAccount_SPLTokenProgram);
        DECLARE_ACCOUNT(7,   metaplex_program_account,      ReadOnly,   NotSigner,  KnownAccount_MetaplexProgram);
        DECLARE_ACCOUNT(8,   rent_sysvar_account,           ReadOnly,   NotSigner,  KnownAccount_RentSysvar);
    }
    
    // There are 4 accounts per entry following the 9 fixed accounts
    uint8_t entry_count = (params->ka_num - 9) / 4;

    sol_log_64(params->ka_num, entry_count, 9 + (entry_count * 4), _account_num, 0);

    // Must be exactly the fixed accounts + 4 accounts per entry
    DECLARE_ACCOUNTS_NUMBER(9 + (entry_count * 4));

    // Ensure the the transaction has been authenticated by the admin
    if (!is_admin_authenticated(config_account, admin_account)) {
        return Error_PermissionDenied;
    }

    // Make sure that the input data is the correct size
    if (params->data_len != compute_add_entries_data_size(entry_count)) {
        return Error_InvalidDataSize;
    }

    // Cast to instruction data
    AddEntriesToBlockData *data = (AddEntriesToBlockData *) params->data;
    
    // Get the validated Block data
    Block *block = get_validated_block(block_account);
    if (!block) {
        return Error_InvalidAccount_First + 3;
    }

    // Fail if the block is already complete
    if (is_block_complete(block)) {
        return Error_InvalidData_First;
    }
    
    // Make sure that the first entry supplied is the next entry to be added to the block
    if (data->first_entry != block->added_entries_count) {
        return Error_InvalidData_First + 1;
    }

    // Make sure that the total number of entries to add does not exceed the number of entries in the block.
    if (entry_count > (block->config.total_entry_count - block->added_entries_count)) {
        return Error_InvalidData_First + 2;
    }

    // Add each entry one by one
    for (uint8_t i = 0; i < entry_count; i++) {
        // This is the group of accounts for this entry
        SolAccountInfo *entry_accounts = &(params->ka[9 + (4 * i)]);

        // This is the entry details for the entry
        EntryDetails *entry_details = &(data->entry_details[i]);

        uint16_t entry_index = data->first_entry + i;

        // Add the entry
        uint64_t result = add_entry(entry_accounts, block_account->key, block, entry_index, funding_account->key,
                                    data, entry_details, params->ka, params->ka_num);
        
        if (result) {
            return result;
        }
    }

    // Now the total number of entries added is increased
    block->added_entries_count += entry_count;

    // If the block has just been completed, then set the block_start_time to the current time, and set the
    // block last_commission_change_epoch so that commission can't be changed this epoch.
    if (is_block_complete(block)) {
        Clock clock;
        if (sol_get_clock_sysvar(&clock)) {
            return Error_FailedToGetClock;
        }
        block->block_start_timestamp = clock.unix_timestamp;
        // If the number of mysteries is 0, then the mystery phase is already done, and so the reveal phase starts
        // immediately.
        if (block->config.total_mystery_count == 0) {
            block->mystery_phase_end_timestamp = clock.unix_timestamp;
        }
        block->last_commission_change_epoch = clock.epoch;
    }
    
    return 0;
}


static uint64_t add_entry(SolAccountInfo *entry_accounts, SolPubkey *block_key, Block *block, uint16_t entry_index,
                          SolPubkey *funding_key, AddEntriesToBlockData *data, EntryDetails *entry_details,
                          SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    SolAccountInfo *entry_account =                     &(entry_accounts[0]);
    SolAccountInfo *mint_account =                      &(entry_accounts[1]);
    SolAccountInfo *token_account =                     &(entry_accounts[2]);
    SolAccountInfo *metaplex_metadata_account =         &(entry_accounts[3]);

    // Check permissions
    if (!entry_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 9;
    }
    if (!mint_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 10;
    }
    if (!token_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 11;
    }
    if (!metaplex_metadata_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 12;
    }

    // Create the mint account
    uint64_t ret = create_entry_mint_account(mint_account, block_key, entry_index, funding_key, transaction_accounts,
                                             transaction_accounts_len);
    if (ret) {
        return ret;
    }

    // Create the entry token account
    ret = create_entry_token_account(token_account, mint_account->key, funding_key, transaction_accounts,
                                     transaction_accounts_len);
    if (ret) {
        return ret;
    }

    // Mint one instance of the token into the token account
    ret = mint_tokens(mint_account->key, token_account->key, 1, transaction_accounts, transaction_accounts_len);

    if (ret) {
        return ret;
    }

    // Create the metaplex metadata
    ret = create_entry_metaplex_metadata(metaplex_metadata_account->key, mint_account->key,
                                         funding_key, block->config.group_number, block->config.block_number,
                                         entry_index, data->metaplex_metadata_uri,
                                         &(data->metaplex_metadata_creator_1), &(data->metaplex_metadata_creator_2),
                                         transaction_accounts, transaction_accounts_len);
    if (ret) {
        return ret;
    }

    // Revoke the mint authority so that it is truly an NFT
    ret = revoke_mint_authority(mint_account->key, &(Constants.nifty_authority_pubkey), transaction_accounts,
                                transaction_accounts_len);
    if (ret) {
        return ret;
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

    // Create the entry account
    ret = create_entry_account(entry_account, mint_account->key, funding_key, transaction_accounts,
                               transaction_accounts_len);
    if (ret) {
        return ret;
    }

    // Initialize the entry account data
    Entry *entry = (Entry *) (entry_account->data);

    entry->data_type = DataType_Entry;

    entry->block_pubkey = *block_key;

    entry->group_number = block->config.group_number;
    
    entry->block_number = block->config.block_number;

    entry->entry_index = entry_index;

    entry->mint_pubkey = *(mint_account->key);
    
    entry->token_pubkey = *(token_account->key);
    
    entry->metaplex_metadata_pubkey = *(metaplex_metadata_account->key);

    entry->minimum_price_lamports = block->config.minimum_price_lamports;

    entry->has_auction = block->config.has_auction;

    if (entry->has_auction) {
        entry->duration = block->config.duration;
    }
    else {
        entry->non_auction_start_price_lamports = block->config.non_auction_start_price_lamports;
    }

    entry->reveal_sha256 = entry_details->sha256;

    return 0;
}
