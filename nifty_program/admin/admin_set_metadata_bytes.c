#pragma once

// Account references:
// 0. `[]` Program config account
// 1. `[SIGNER]` -- This must be the admin account
// 2. `[WRITE]` -- The block account
// 3. `[ENTRY]` -- The entry account

#include "inc/clock.h"
#include "util/util_accounts.c"
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

    // Get the instruction data
    if (params->data_len < sizeof(SetMetadataBytesData)) {
        return Error_InvalidDataSize;
    }

    SetMetadataBytesData *data = (SetMetadataBytesData *) params->data;

    // Get the block data
    Block *block = get_validated_block(block_account);
    if (!block) {
        return Error_InvalidAccount_First + 2;
    }

    // Get the entry data
    Entry *entry = get_validated_entry(block, data->entry_index, entry_account);
    if (!entry) {
        return Error_InvalidAccount_First + 3;
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

    if (params->data_len != compute_set_metadata_bytes_data_size(data->bytes_count)) {
        return Error_InvalidDataSize;
    }

    // Check to make sure that the data written will not go beyond the bounds of the metadata
    if (((uint32_t) data->start_index + (uint32_t) data->bytes_count) > sizeof(EntryMetadata)) {
        return Error_InvalidData_First + 1;
    }

    // The entry can only have its metadata set if it's waiting for reveal
    if (is_entry_revealed(entry)) {
        return Error_AlreadyRevealed;
    }
    
    // All checks passed, set the data
    uint8_t *metadata_buffer = (uint8_t *) &(entry->metadata);
        
    sol_memcpy(&(metadata_buffer[data->start_index]), data->bytes, data->bytes_count);

    // All done
    
    return 0;
}
