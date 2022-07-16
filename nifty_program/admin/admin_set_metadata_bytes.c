#pragma once

#include "inc/clock.h"
#include "util/util_accounts.c"
#include "util/util_block.c"


typedef struct
{
    // This is the instruction code for SetMetadataBytes
    uint8_t instruction_code;

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


// XXX This should be updated to use compression.  It would be best to have a separate on-chain program that
// does decompression, and then have this program invoke it.  The decompression program would take:
// - An account with compressed data in it
// - An account to decompress into
// - An { offset, length } within the destination account to decompress to
// - Would decompress the input data into the output
// With the above, all metadata for an entry could likely be uploaded in a single transaction.
// It may be possible to upload metadata for multiple entries in a single transaction even.

static uint64_t admin_set_metadata_bytes(SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,   config_account,                ReadOnly,   NotSigner,  KnownAccount_ProgramConfig);
        DECLARE_ACCOUNT(1,   admin_account,                 ReadOnly,   Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,   block_account,                 ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(3,   entry_account,                 ReadWrite,  NotSigner,  KnownAccount_NotKnown);
    }

    DECLARE_ACCOUNTS_NUMBER(4);

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
    Entry *entry = get_validated_entry_of_block(entry_account, block_account->key);
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
    
    // The entry can only have its metadata set if it's waiting for reveal, i.e. in a PreReveal state
    // Not passing in block because don't care which exact PreReveal state it is
    if (get_entry_state(0, entry, &clock) != EntryState_PreReveal) {
        return Error_AlreadyRevealed;
    }
    
    // All checks passed, set the data
    uint8_t *metadata_buffer = (uint8_t *) &(entry->metadata);
        
    sol_memcpy(&(metadata_buffer[data->start_index]), data->bytes, data->bytes_count);

    // All done
    
    return 0;
}
