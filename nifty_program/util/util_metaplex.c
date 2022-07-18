#pragma once

#include "inc/constants.h"
#include "inc/entry.h"
#include "util/util_borsh.c"


// Metaplex Metadata is serialized Borsh format; there are only three possible forms that are supported by this
// implementation: one with no creators, one with two creators, and one with three creators

typedef struct __attribute__((__packed__))
{
    uint32_t name_len; // Always set to 32
    uint8_t name[32];

    uint32_t symbol_len; // Always set to 10
    uint8_t symbol[10];

    uint32_t uri_len; // Always set to 200
    uint8_t uri[200];

    uint16_t seller_fee_basis_points;

    bool has_creators; // Always set to false

    bool has_collections; // Always set to false
    
    bool has_uses; // Always set to false
    
} MetaplexMetadataZeroCreators;

typedef struct __attribute__((__packed__))
{
    uint32_t name_len; // Always set to 32
    uint8_t name[32];

    uint32_t symbol_len; // Always set to 10
    uint8_t symbol[10];

    uint32_t uri_len; // Always set to 200
    uint8_t uri[200];

    uint16_t seller_fee_basis_points;

    bool has_creators; // Always set to true
    uint32_t creator_count; // Always set to 2
    SolPubkey creator_1_pubkey;
    bool creator_1_verified;
    uint8_t creator_1_share;
    SolPubkey creator_2_pubkey;
    bool creator_2_verified;
    uint8_t creator_2_share;

    bool has_collections; // Always set to false
    
    bool has_uses; // Always set to false
    
} MetaplexMetadataTwoCreators;

typedef struct __attribute__((__packed__))
{
    uint32_t name_len; // Always set to 32
    uint8_t name[32];

    uint32_t symbol_len; // Always set to 10
    uint8_t symbol[10];

    uint32_t uri_len; // Always set to 200
    uint8_t uri[200];

    uint16_t seller_fee_basis_points;

    bool has_creators; // Always set to true
    uint32_t creator_count; // Always set to 3
    SolPubkey creator_1_pubkey;
    bool creator_1_verified;
    uint8_t creator_1_share;
    SolPubkey creator_2_pubkey;
    bool creator_2_verified;
    uint8_t creator_2_share;
    SolPubkey creator_3_pubkey;
    bool creator_3_verified;
    uint8_t creator_3_share;

    bool has_collections; // Always set to false
    
    bool has_uses; // Always set to false
    
} MetaplexMetadataThreeCreators;

// Use the maximum size possible
#define METAPLEX_METADATA_DATA_SIZE sizeof(MetaplexMetadataThreeCreators)


static void number_string(uint8_t *buf, uint32_t number, uint8_t digits)
{
    buf = &(buf[digits - 1]);

    while (digits--) {
        *buf-- = '0' + (number % 10);
        number /= 10;
    }
}


// [data] must have at least METAPLEX_METADATA_DATA_SIZE bytes in it.  Returns the pointer to the byte immediately
// after the end of data
static uint8_t *encode_metaplex_metadata(uint8_t *data, uint8_t *name, uint8_t *symbol, uint8_t *uri,
                                         SolPubkey *creator_1, SolPubkey *creator_2, SolPubkey *authority_key)
{
    // If the first pubkey is empty swap the pubkeys so that the logic below can readily test on 0, 1 or 2 creators
    if (is_empty_pubkey(creator_1)) {
        SolPubkey *tmp = creator_1;
        creator_1 = creator_2;
        creator_2 = tmp;
    }

    if (is_empty_pubkey(creator_1)) {
        MetaplexMetadataZeroCreators *metadata = (MetaplexMetadataZeroCreators *) data;
        sol_memset(metadata, 0, sizeof(*metadata));
        metadata->name_len = 32;
        uint32_t len = sol_strlen((const char *) name);
        if (len > metadata->name_len) {
            len = metadata->name_len;
        }
        sol_memcpy(metadata->name, name, len);
        metadata->symbol_len = 10;
        len = sol_strlen((const char *) symbol);
        if (len > metadata->symbol_len) {
            len = metadata->symbol_len;
        }
        sol_memcpy(metadata->symbol, symbol, len);
        metadata->uri_len = 200;
        len = sol_strlen((const char *) uri);
        if (len > metadata->uri_len) {
            len = metadata->uri_len;
        }
        sol_memcpy(metadata->uri, uri, len);
        return &(data[sizeof(*metadata)]);
    }
    else if (is_empty_pubkey(creator_2)) {
        MetaplexMetadataTwoCreators *metadata = (MetaplexMetadataTwoCreators *) data;
        sol_memset(metadata, 0, sizeof(*metadata));
        metadata->name_len = 32;
        uint32_t len = sol_strlen((const char *) name);
        if (len > metadata->name_len) {
            len = metadata->name_len;
        }
        sol_memcpy(metadata->name, name, len);
        metadata->symbol_len = 10;
        len = sol_strlen((const char *) symbol);
        if (len > metadata->symbol_len) {
            len = metadata->symbol_len;
        }
        sol_memcpy(metadata->symbol, symbol, len);
        metadata->uri_len = 200;
        len = sol_strlen((const char *) uri);
        if (len > metadata->uri_len) {
            len = metadata->uri_len;
        }
        sol_memcpy(metadata->uri, uri, len);
        metadata->has_creators = true;
        metadata->creator_count = 2;
        metadata->creator_1_pubkey = *creator_1;
        metadata->creator_1_share = 100;
        metadata->creator_2_pubkey = *authority_key;
        return &(data[sizeof(*metadata)]);
    }
    else {
        MetaplexMetadataThreeCreators *metadata = (MetaplexMetadataThreeCreators *) data;
        sol_memset(metadata, 0, sizeof(*metadata));
        metadata->name_len = 32;
        uint32_t len = sol_strlen((const char *) name);
        if (len > metadata->name_len) {
            len = metadata->name_len;
        }
        sol_memcpy(metadata->name, name, len);
        metadata->symbol_len = 10;
        len = sol_strlen((const char *) symbol);
        if (len > metadata->symbol_len) {
            len = metadata->symbol_len;
        }
        sol_memcpy(metadata->symbol, symbol, len);
        metadata->uri_len = 200;
        len = sol_strlen((const char *) uri);
        if (len > metadata->uri_len) {
            len = metadata->uri_len;
        }
        sol_memcpy(metadata->uri, uri, len);
        metadata->has_creators = true;
        metadata->creator_count = 3;
        metadata->creator_1_pubkey = *creator_1;
        metadata->creator_1_share = 50;
        metadata->creator_2_pubkey = *creator_2;
        metadata->creator_2_share = 50;
        metadata->creator_3_pubkey = *authority_key;
        return &(data[sizeof(*metadata)]);
    }
}


// Ensures that the metadata is being created at the correct address, returns an error if not
static uint64_t create_metaplex_metadata(SolPubkey *metaplex_metadata_key, SolPubkey *mint_key, SolPubkey *funding_key,
                                         uint8_t *name, uint8_t *symbol, uint8_t *uri, SolPubkey *creator_1,
                                         SolPubkey *creator_2,
                                         // All cross-program invocation must pass all account infos through, it's
                                         // the only sane way to cross-program invoke
                                         SolAccountInfo *transaction_accounts, int transaction_accounts_len)

{
    // It is not necessary to verify that the metaplex_metadata_key is the correct account for the given mint,
    // because the Metaplex Metata program already does this
    
    SolInstruction instruction;

    instruction.program_id = &(Constants.metaplex_program_pubkey);
    
    SolAccountMeta account_metas[] =
          // Metadata key
        { { /* pubkey */ metaplex_metadata_key, /* is_writable */ true, /* is_signer */ false },
          // Mint of token asset
          { /* pubkey */ mint_key, /* is_writable */ false, /* is_signer */ false },
          // Mint authority
          { /* pubkey */ &(Constants.nifty_authority_pubkey), /* is_writable */ false, /* is_signer */ true },
          // payer
          { /* pubkey */ funding_key, /* is_writable */ true, /* is_signer */ true },
          // update authority info
          { /* pubkey */ &(Constants.nifty_authority_pubkey), /* is_writable */ false, /* is_signer */ false },
          // system program
          { /* pubkey */ &((* (_Constants *) &Constants).system_program_pubkey), /* is_writable */ false,
            /* is_signer */ false },
          // rent
          { /* pubkey */ &(Constants.rent_sysvar_pubkey), /* is_writable */ false, /* is_signer */ false } };
    
    instruction.accounts = account_metas;
    instruction.account_len = ARRAY_LEN(account_metas);
    
    // Encoding the data for metaplex requires using Borsch serialize format, eugh.
    uint8_t data[BORSH_SIZE_U8 /* instruction code */ +
                 METAPLEX_METADATA_DATA_SIZE /* data */ +
                 BORSH_SIZE_BOOL /* is_mutable */];
    uint8_t *d = borsh_encode_u8(data, 16); // instruction code 16 = CreateMetadataAccountV2
    d = encode_metaplex_metadata(d, name, symbol, uri, creator_1, creator_2, &(Constants.nifty_authority_pubkey));
    d = borsh_encode_bool(d, true); // is_mutable
    
    instruction.data = data;
    instruction.data_len = ((uint64_t) d) - ((uint64_t) instruction.data);

    // Must invoke with signed authority account
    uint8_t *seed_bytes = (uint8_t *) Constants.nifty_authority_seed_bytes;
    SolSignerSeed seed = { seed_bytes, sizeof(Constants.nifty_authority_seed_bytes) };
    SolSignerSeeds signer_seeds = { &seed, 1 };

    return sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
}


// Change the update authority for metaplex metadata.  Assumes that the nifty authority is the current authority
// of the metadata.
static uint64_t set_metaplex_metadata_authority(SolPubkey *metaplex_metadata_pubkey, SolPubkey *new_authority,
                                                SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    SolInstruction instruction;

    instruction.program_id = &(Constants.metaplex_program_pubkey);
    
    // UpdateMetadataAccountV2
    SolAccountMeta account_metas[] =
          // Metadata key
        { { /* pubkey */ metaplex_metadata_pubkey, /* is_writable */ true, /* is_signer */ false },
          // Update authority
          { /* pubkey */ &(Constants.nifty_authority_pubkey), /* is_writable */ false, /* is_signer */ true } };

    instruction.accounts = account_metas;
    instruction.account_len = ARRAY_LEN(account_metas);
    
    // The data is a very simple borsh serialized format
    uint8_t data[1 /* Instruction code UpdateMetadataAccountV2 */ +
                 1 /* None */ +
                 1 + 32 /* Some(new_authority) */ +
                 1 /* None */ +
                 1 /* None */];
    sol_memset(&data, 0, sizeof(data));
    data[0] = 15;
    data[2] = 1;
    * ((SolPubkey *) &(data[3])) = *new_authority;
    
    instruction.data = (uint8_t *) data;
    instruction.data_len = sizeof(data);

    // Must invoke with signed authority account
    uint8_t *seed_bytes = (uint8_t *) Constants.nifty_authority_seed_bytes;
    SolSignerSeed seed = { seed_bytes, sizeof(Constants.nifty_authority_seed_bytes) };
    SolSignerSeeds signer_seeds = { &seed, 1 };
    
    return sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
}


// Sets the primary_sale_happened metaplex metadata value to true
static uint64_t set_metaplex_metadata_primary_sale_happened(Entry *entry,
                                                            // All cross-program invocation must pass all account
                                                            // infos through, it's the only sane way to cross-program
                                                            // invoke
                                                            SolAccountInfo *transaction_accounts,
                                                            int transaction_accounts_len)
{
    SolInstruction instruction;

    instruction.program_id = &(Constants.metaplex_program_pubkey);
    
    // UpdateMetadataAccountV2
    SolAccountMeta account_metas[] =
          // Metadata key
        { { /* pubkey */ &(entry->metaplex_metadata_pubkey), /* is_writable */ true, /* is_signer */ false },
          // Update authority
          { /* pubkey */ &(Constants.nifty_authority_pubkey), /* is_writable */ false, /* is_signer */ true } };

    instruction.accounts = account_metas;
    instruction.account_len = ARRAY_LEN(account_metas);
    
    // The data is a very simple borsh serialized format
    uint8_t data[1 /* Instruction code UpdateMetadataAccountV2 */ +
                 1 /* None */ +
                 1 /* None */ +
                 2 /* Some(true) */ +
                 1 /* None */]
        = { 15, 0, 0, 1, 1, 0 };
    
    instruction.data = (uint8_t *) data;
    instruction.data_len = sizeof(data);

    // Must invoke with signed authority account
    uint8_t *seed_bytes = (uint8_t *) Constants.nifty_authority_seed_bytes;
    SolSignerSeed seed = { seed_bytes, sizeof(Constants.nifty_authority_seed_bytes) };
    SolSignerSeeds signer_seeds = { &seed, 1 };
    
    return sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
}

    
// Set the metaplex metadata for the entry to that of the given level
static uint64_t set_metaplex_metadata_for_level(Entry *entry, uint8_t level, SolAccountInfo *metaplex_metadata_account,
                                                // All cross-program invocation must pass all account infos through,
                                                // it's the only sane way to cross-program invoke
                                                SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    // The values to update are name and uri.  Symbol is always "SHIN".  seller_fee_basis_points is always 0,
    // and collection and uses are always empty.  What must be read from the existing metadata is the
    // creators array so that it can be re-used in the new metadata.

    // Decode the metadata
    uint8_t name[200 + 1];
    uint8_t symbol[10 + 1];
    uint8_t uri[200 + 1];

    uint32_t creator_count;
    SolPubkey creator_keys[3];

    {
        const uint8_t *data = metaplex_metadata_account->data;
        uint32_t data_len = (uint32_t) metaplex_metadata_account->data_len;

        // Skip these:
        // pub key: Key,  -- 1 byte
        // pub update_authority: Pubkey, -- 32 bytes
        // pub mint: Pubkey, -- 32 bytes
        if (data_len < (1 + 32 + 32)) {
            return Error_InvalidMetadataValues;
        }
        data = &(data[1 + 32 + 32]);
        data_len -= (1 + 32 + 32);
        
        // Decode name, string of at most 200 characters
        uint32_t ret = borsh_decode_string(data, data_len, sizeof(name) - 1, name);
        if (!ret) {
            return Error_InvalidMetadataValues;
        }
        data += ret, data_len -= ret;

        // Decode symbol, string of at most 4 characters
        ret = borsh_decode_string(data, data_len, sizeof(symbol) - 1, symbol);
        if (!ret) {
            return Error_InvalidMetadataValues;
        }
        data += ret, data_len -= ret;
        
        // Decode uri, string of at most 200 characters
        ret = borsh_decode_string(data, data_len, sizeof(uri) - 1, uri);
        if (!ret) {
            return Error_InvalidMetadataValues;
        }
        data += ret, data_len -= ret;
        
        // Decode seller_fee_basis_points, which is ignored
        {
            uint16_t seller_fee_basis_points;
            ret = borsh_decode_u16(data, data_len, &seller_fee_basis_points);
            if (!ret) {
                return Error_InvalidMetadataValues;
            }
            data += ret, data_len -= ret;

        }

        // Read creators, which may be None or Some(vector of creator)
        bool option;
        ret = borsh_decode_option(data, data_len, &option);
        if (!ret) {
            return Error_InvalidMetadataValues;
        }
        data += ret, data_len -= ret;
        
        if (option) {
            // Read number of elements in vector
            ret = borsh_decode_vec_count(data, data_len, &creator_count);
            if (!ret) {
                return Error_InvalidMetadataValues;
            }
            data += ret, data_len -= ret;
            // Must be 2 or 3 creators
            if ((creator_count < 2) || (creator_count > 3)) {
                return Error_InvalidMetadataValues;
            }

            // Read each creator
            for (uint32_t i = 0; i < creator_count; i++) {
                // Decode creator pubkey
                ret = borsh_decode_pubkey(data, data_len, &(creator_keys[i]));
                if (!ret) {
                    return Error_InvalidMetadataValues;
                }
                data += ret, data_len -= ret;
                // Decode verified status of creator, which is ignored
                bool verified;
                ret = borsh_decode_bool(data, data_len, &verified);
                if (!ret) {
                    return Error_InvalidMetadataValues;
                }
                data += ret, data_len -= ret;
                // Decode creator share, which is ignored
                uint8_t share;
                ret = borsh_decode_u8(data, data_len, &share);
                if (!ret) {
                    return Error_InvalidMetadataValues;
                }
                data += ret, data_len -= ret;
                i++;
            }
        }
        else {
            creator_count = 0;
        }
    }

    LevelMetadata *level_metadata = &(entry->metadata.level_metadata[level]);

    // creator_1 is always the first creator
    SolPubkey *creator_1 = &(creator_keys[0]);
    SolPubkey *creator_2 = &(creator_keys[1]);
    if (creator_count == 2) {
        // creator_2 is an empty pubkey if there are only 2 creators
        sol_memset(&(creator_keys[1]), 0, sizeof(creator_keys[1]));
    }

    // UpdateMetadataAccountV2
    SolInstruction instruction;

    instruction.program_id = &(Constants.metaplex_program_pubkey);

    SolAccountMeta account_metas[] =
          // Metadata key
        { { /* pubkey */ &(entry->metaplex_metadata_pubkey), /* is_writable */ true, /* is_signer */ false },
          // Update authority
          { /* pubkey */ &(Constants.nifty_authority_pubkey), /* is_writable */ false, /* is_signer */ true } };

    instruction.accounts = account_metas;
    instruction.account_len = ARRAY_LEN(account_metas);
    
    // Encoding the data for metaplex requires using Borsch serialize format, eugh.
    uint8_t data[BORSH_SIZE_U8 /* instruction code */ +
                 BORSH_SIZE_OPTION(METAPLEX_METADATA_DATA_SIZE) /* data */ +
                 BORSH_SIZE_OPTION(0) /* update_authority (None) */ +
                 BORSH_SIZE_OPTION(0) /* primary_sale_happened (None) */ +
                 BORSH_SIZE_OPTION(0) /* is_mutable (None) */];

    uint8_t *d = borsh_encode_u8(data, 15); // instruction code 15 = UpdateMetadataAccountV2
    d = borsh_encode_option_some(d);
    d = encode_metaplex_metadata(d, level_metadata->name, (uint8_t *) "SHIN", level_metadata->uri, creator_1,
                                 creator_2, &(Constants.nifty_authority_pubkey));
    d = borsh_encode_option_none(d); // update_authority
    d = borsh_encode_option_none(d); // primary_sale_happened
    d = borsh_encode_option_none(d); // is_mutable
    
    instruction.data = data;
    instruction.data_len = ((uint64_t) d) - ((uint64_t) data);

    // Must invoke with signed authority account
    uint8_t *seed_bytes = (uint8_t *) Constants.nifty_authority_seed_bytes;
    SolSignerSeed seed = { seed_bytes, sizeof(Constants.nifty_authority_seed_bytes) };
    SolSignerSeeds signer_seeds = { &seed, 1 };
    
    return sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
}
