
// Account references:
// 0. `[]` Program config account
// 1. `[SIGNER]` -- This must be the admin account
// 2. `[WRITE]` -- The block account
// 3. `[ENTRY]` -- The entry account

#include "inc/clock.h"
#include "util/util_block.c"


typedef struct
{
    // This is the instruction code for SetMetadataBytes
    uint8_t instruction_code;

    // Index of the entry whose metadata bytes are to be set
    uint16_t entry_index;

    // Index of the first byte of metadata to set
    uint16_t start_index;

    // Number of bytes of metadata to set
    uint16_t bytes_count;

    // The bytes of metadata to copy in
    uint8_t bytes[];
    
} SetMetadataBytesData;


static uint64_t compute_set_metadata_bytes_data_size(uint16_t bytes_count)
{
    SetMetadataBytesData *b = 0;
    
    return ((uint64_t) &(b->bytes[bytes_count]));
}


static uint64_t admin_set_metadata_bytes(SolParameters *params)
{
    // Sanitize the accounts.  There must be 4.
    if (params->ka_num != 4) {
        return Error_IncorrectNumberOfAccounts;
    }

    SolAccountInfo *config_account = &(params->ka[0]);
    SolAccountInfo *admin_account = &(params->ka[1]);
    SolAccountInfo *block_account = &(params->ka[2]);
    SolAccountInfo *entry_account = &(params->ka[3]);

    // Ensure the the transaction has been authenticated by the admin
    if (!is_admin_authenticated(config_account, admin_account)) {
        return Error_PermissionDenied;
    }

    // Get the block data
    if (block_account->data_len < sizeof(Block)) {
        return Error_InvalidAccount_First + 2;
    }

    Block *block = (Block *) block_account->data;
    
    if (block->data_type != DataType_Block) {
        return Error_InvalidAccount_First + 2;
    }

    // Make sure that the block is complete; can't be setting metadata into blocks for which all the entries
    // have not been added yet
    if (!is_block_complete(block)) {
        return Error_BlockNotComplete;
    }

    // Make sure that the block has achieved its reveal criteria
    Clock clock;
    uint64_t retval = sol_get_clock_sysvar(&clock);
    if (retval) {
        return retval;
    }
    if (!is_complete_block_revealable(block, &clock)) {
        return Error_BlockNotRevealable;
    }

    // Get the instruction data
    if (params->data_len < sizeof(SetMetadataBytesData)) {
        return Error_InvalidDataSize;
    }

    SetMetadataBytesData *data = (SetMetadataBytesData *) params->data;

    if (params->data_len != compute_set_metadata_bytes_data_size(data->bytes_count)) {
        return Error_InvalidDataSize;
    }

    // Check the entry index to make sure it's valid
    if (data->entry_index >= block->config.total_entry_count) {
        return Error_InvalidData_First;
    }

    // Check to make sure that the data written will not go beyond the bounds of the metadata
    if (((uint32_t) data->start_index + (uint32_t) data->bytes_count) > sizeof(EntryMetadata)) {
        return Error_InvalidData_First + 1;
    }

    // Check the Entry account to ensure that it's the correct Entry account for this entry
    SolPubkey entry_address;
    if (compute_entry_address(block->config.group_number, block->config.block_number, data->entry_index,
                              block->entry_bump_seeds[data->entry_index], &entry_address) ||
        !SolPubkey_same(&entry_address, entry_account->key)) {
        return Error_InvalidAccount_First + 3;
    }

    // Get the Entry data
    if (entry_account->data_len < sizeof(Entry)) {
        return Error_InvalidData_First + 4;
    }

    Entry *entry = (Entry *) entry_account->data;

    if (entry->data_type != DataType_Entry) {
        return Error_InvalidAccount_First + 5;
    }

    // If the entry has already been revealed, then cannot change its metadata
    if ((entry->state != EntryState_PreRevealUnowned) && (entry->state != EntryState_PreRevealOwned)) {
        return Error_AlreadyRevealed;
    }

    // All checks passed, set the data
    uint8_t *metadata_buffer = (uint8_t *) &(entry->metadata);
        
    sol_memcpy(&(metadata_buffer[data->start_index]), data->bytes, data->bytes_count);

    // All done
    
    return 0;
}
