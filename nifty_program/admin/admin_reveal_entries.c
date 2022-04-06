
// Account references:
// 0. `[]` Program config account -- this must be g_program_config_account_address
// 1. `[SIGNER]` -- This must be the admin account
// 2. `[WRITE]` -- The block account address
// 3. `[]` -- Token mint account
// 4. `[]` -- Token account
// 5. `[]` -- Metaplex metadata account
// 6. `[WRITE]` -- Entry account
// (Repeat 3-6 for each additional entry in the transaction)

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
    return (3 + (4 * entry_index));
}


static uint8_t index_of_reveal_token_account(uint8_t entry_index)
{
    return (4 + (4 * entry_index));
}


static uint8_t index_of_reveal_metaplex_metadata_account(uint8_t entry_index)
{
    return (5 + (4 * entry_index));
}


static uint8_t index_of_reveal_entry_account(uint8_t entry_index)
{
    return (6 + (4 * entry_index));
}


// Before an entry is revealed, it holds a stand-in SHA-256 value that is used to ensure that only the original data
// associated with that entry is replaced into the entry at the time of reveal.  The three pieces of data that
// comprise a nifty stakes NFT is: the mint of the NFT (which identifies the actual NFT), the metaplex metadata (which
// gives the NFT its metaplex qualities), and the Shinobi metadata (which gives the NFT its Shinobi qualities).
// Therefore, all three are incorporated into the SHA-256 has that is first listed with the entry before it is
// revealed.  In addition, a 64 bit salt value is included in the data that is hashed to ensure that it is not
// possible for anyone to guess the entry's corresponding NFT if they happen to find out (through searching on-chain
// data for example) the identity of an NFT.  If the salt were not used, then someone could simply look at all NFTs
// owned by the nifty program, calculate their hash value (because their mint account, metaplex data, and shinobi data
// is available publicly), and then find the entry with that hash and know what NFT the entry corresponds to.
// The method for calculating an entry hash is to hash the SHA-256 of the mint account address, then totality of
// the metaplex metadata, and the totality of the shinobi metadata, as three separate SHA-256 hashes.  Then these
// are concatenated together along with the salt at the end, and this total set of 32 + 32 + 32 + 8 bytes is hashed
// once again into the final SHA-256 hash.  If instead the raw data (nft mint address, metaplex metadata, shinobi
// metadata, and salt) where hashed all at once, it would require the nifty program to compose a single large buffer
// containing all of these values, which would require allocation of an arbitrarily long buffer, which is avoided
// by doing the two-step hashing.

static uint64_t do_reveal_entries(SolParameters *params)
{
    // Sanitize the accounts.  There must be at least 3
    if (params->ka_num < 3) {
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

    // Data can be used now
    RevealEntriesData *data = (RevealEntriesData *) params->data;

    // Make sure that the data is properly sized given the number of entries
    if (params->data_len != compute_reveal_entries_data_size(data->entry_count)) {
        return Error_InvalidDataSize;
    }

    // Make sure that the number of accounts in the instruction account list is 3 + (2 * the number of entries in
    // the transaction), because each NFT account to reveal in sequence has four corresponding accounts from the
    // instruction accounts array: 1. Mint account, 2. Metaplex metadata account
    if (params->ka_num != (3 + (4 * data->entry_count))) {
        return Error_InvalidData_First;
    }

    // Here is the existing block data
    Block *block = (Block *) block_account->data;

    // If the block does not have the correct data type, then this is an error (admin accidentally treating a bid as a
    // block?)
    if (block->data_type != DataType_Block) {
        return Error_IncorrectAccountType;
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
    if (!is_block_reveal_criteria_met(block, &clock)) {
        return Error_BlockNotRevealable;
    }
    
    // Make sure that the last entry to reveal does not exceed the number of entries in the block
    if ((data->first_entry + data->entry_count) > block->config.total_entry_count) {
        return Error_InvalidData_First + 1;
    }

    // Reveal entries one by one
    for (uint16_t i = 0; i < data->entry_count; i++) {
        uint16_t destination_index = data->first_entry + i;

        // This is the account info of the NFT mint, as passed into the accounts list
        SolAccountInfo *mint_account_info = &(params->ka[index_of_reveal_mint_account(i)]);
        
        // This is the token account of the NFT, as passed into the accounts list
        SolAccountInfo *token_account_info = &(params->ka[index_of_reveal_token_account(i)]);
        
        // This is the account info of the metaplex metadata for the NFT, as passed into the accounts list
        SolAccountInfo *metaplex_metadata_account_info = &(params->ka[index_of_reveal_metaplex_metadata_account(i)]);

        // This is the account info of the entry, as passed into the accounts list
        SolAccountInfo *entry_account_info = &(params->ka[index_of_reveal_entry_account(i)]);
        
        // The salt is the corresponding entry in the input data
        salt_t salt = data->entry_salt[i];

        // Do a single reveal of this entry
        uint64_t result = reveal_single_entry(block, &clock, salt, mint_account_info, token_account_info,
                                              metaplex_metadata_account_info, entry_account_info);

        // If that reveal failed, then the entire transaction fails
        if (result) {
            return result;
        }
    }

    // All entries revealed successfully
    
    return 0;
}
