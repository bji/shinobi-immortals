
// Account references:
// 0. `[]` Program config account -- this must be g_program_config_account_address
// 1. `[SIGNER]` -- This must be the admin account
// 2. `[WRITE]` -- The block account address

// The metadata program will have an entrypoint for creating metadata associated with a mint, just like metaplex.
// It will also have an entry point for "incorporating changes in the entry into its metadata".  This will:
// - Read Shinobi Systems metadata for the mint
// - Find the block address + entry number in the Shinobi Systems metadata
// - Look up the block and the entry values therein
// - Incorporate changes into the Shinobi Systems metadata
// - Incorporate changes into the metaplex metadata too

// Eventually there may be a replacement for this program, and when there is:
// - its program id will be written into the global config
// - Then the user will be able to invoke a function on the Shinobi systems metadata program: "update
//   metadata program id".  This will check to see if the metadata program id is the "next one" in the global
//   list of metadata program ids.  If it is, it is called on its "upgrade function", which will produce
//   new metadata.  The metadata will then be overwritten with that.  And the "updated metadata program id"
//   value will be written into the metadata.
//   - Forever thereafter, when the metadata program is invoked, it passes its input data to the updated metadata
//     program id thus written.  If that succeeds, it overwrites the metadata with the resulting values.
//   - Also set the metaplex update_authority to the new metadata program

// The above allows the metadata program to essentially be "replaced" later, but only if the user agrees to it.

// It also allows 'valid' metadata to be identified by having the Shinobi Systems metadata as its owner.


typedef struct
{
    // This is the instruction code for AddEntriesToBlockData
    uint8_t instruction_code;
    
    // Index of first entry included here
    uint16_t first_entry;

    // Total number of entries in the entry_seeds array
    uint16_t entry_count;
    
    seed_t entry_seeds[0];
    
} AddEntriesToBlockData;


static uint64_t compute_add_entries_data_size(uint16_t entry_count)
{
    AddEntriesToBlockData *d = 0;

    // The total space needed is from the beginning of AddEntriesToBlockData to the entries element one beyond the
    // total supported (i.e. if there are 100 entries, then then entry at index 100 starts at the first byte beyond
    // the array)
    return ((uint64_t) &(d->entry_seeds[entry_count]));
}

    
static uint64_t do_add_entries_to_block(SolParameters *params)
{
    // Sanitize the accounts.  There must be exactly 3.
    if (params->ka_num != 3) {
        return Error_IncorrectNumberOfAccounts;
    }

    SolAccountInfo *config_account = &(params->ka[0]);
    SolAccountInfo *admin_account = &(params->ka[1]);
    SolAccountInfo *block_account = &(params->ka[2]);

    // Ensure the the transaction has been authenticated by the admin
    if (!is_admin_authenticated(config_account, admin_account)) {
        return Error_PermissionDenied;
    }

    // Ensure that the block account is writable
    if (!block_account->is_writable) {
        return Error_BadPermissions;
    }

    // Ensure that the block account is owned by the program
    if (!is_nifty_stakes_program(block_account->owner)) {
        return Error_InvalidAccount_First + 2;
    }

    // Ensure that the instruction data has the minimum possible size
    if (params->data_len < sizeof(AddEntriesToBlockData)) {
        return Error_InvalidDataSize;
    }

    // Data can be used now
    AddEntriesToBlockData *data = (AddEntriesToBlockData *) params->data;

    // Make sure that the data is properly sized given the number of entries
    if (params->data_len != compute_add_entries_data_size(data->entry_count)) {
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
    if (data->entry_count > (block->config.total_entry_count - block->state.added_entries_count)) {
        return Error_InvalidData_First + 1;
    }

    // Copy the entries in
    sol_memcpy(&(block->entry_seeds[block->state.added_entries_count]), data->entry_seeds,
               sizeof(seed_t) * data->entry_count);

    // Now the total number of entries added is increased
    block->state.added_entries_count += data->entry_count;

    // If the block has just been completed, then set the block_start_time to the current time.
    if (is_block_complete(block)) {
        Clock clock;
        if (sol_get_clock_sysvar(&clock)) {
            return Error_FailedToGetClock;
        }
        block->state.block_start_timestamp = clock.unix_timestamp;
    }
    
    return 0;
}
