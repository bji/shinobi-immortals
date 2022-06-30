#pragma once

#include "inc/constants.h"
#include "util/util_borsh.c"


static void number_string(uint8_t *buf, uint32_t number, uint8_t digits)
{
    buf = &(buf[digits - 1]);

    while (digits--) {
        *buf-- = '0' + (number % 10);
        number /= 10;
    }
}


#define METAPLEX_METADATA_DATA_SIZE                                                                                    \
    (/* name */                     BORSH_SIZE_STRING(200) +                                                           \
     /* symbol "SHIN" */            BORSH_SIZE_STRING(4) +                                                             \
     /* uri */                      BORSH_SIZE_STRING(200) +                                                           \
     /* seller_fee_basis_points */  BORSH_SIZE_U16 +                                                                   \
     /* (option) creator vec */     BORSH_SIZE_OPTION(BORSH_SIZE_VEC(2,                                                \
     /*   creator pubkey */           BORSH_SIZE_PUBKEY +                                                              \
     /*   creator verified */         BORSH_SIZE_U8 +                                                                  \
     /*   creator share */            BORSH_SIZE_U8)) +                                                                \
     /* (option) collections vec */ BORSH_SIZE_OPTION(BORSH_SIZE_VEC(0,                                                \
     /*   collections verified */     BORSH_SIZE_U8 +                                                                  \
     /*   collections key */          BORSH_SIZE_PUBKEY)) +                                                            \
     /* (option) uses vec */        BORSH_SIZE_OPTION(BORSH_SIZE_VEC(0,                                                \
     /*   uses method */              BORSH_SIZE_ENUM(0) +                                                             \
     /*   uses remaining */           BORSH_SIZE_U64 +                                                                 \
     /*   uses total */               BORSH_SIZE_U64)) +                                                               \
     /* is_mutable */               BORSH_SIZE_BOOL)

// [data] must have at least METAPLEX_METADATA_DATA_SIZE bytes in it.  Returns the pointer to the byte immediately
// after the end of data
static uint8_t *encode_metaplex_metadata(uint8_t *data, const uint8_t *name, uint8_t name_len, uint8_t *metadata_uri,
                                         SolPubkey *creator_1, SolPubkey *creator_2, SolPubkey *authority_key)
{
    uint8_t *encoded = borsh_encode_zero_terminated_string(encoded, name_len, name); // name

    encoded = borsh_encode_zero_terminated_string(encoded, 4, (uint8_t *) "SHIN"); // symbol

    encoded = borsh_encode_zero_terminated_string(encoded, 200, metadata_uri); // metadata_uri

    encoded = borsh_encode_u16(encoded, 0); // seller_fee_basis_points

    // If the first pubkey is empty swap the pubkeys so that the logic below can readily test on 0, 1 or 2 creators
    if (is_empty_pubkey(creator_1)) {
        SolPubkey *tmp = creator_1;
        creator_1 = creator_2;
        creator_2 = tmp;
    }

    if (is_empty_pubkey(creator_1)) {
        encoded = borsh_encode_option_none(encoded); // no creators
    }
    else {
        // Metaplex program requires the update authority to be a creator if there are any creators listed
        encoded = borsh_encode_option_some(encoded);
        bool has_2 = !is_empty_pubkey(creator_2);
        encoded = borsh_encode_vec_count(encoded, has_2 ? 3 : 2); // creator vector count
        encoded = borsh_encode_pubkey(encoded, creator_1); // creator pubkey
        encoded = borsh_encode_bool(encoded, false); // creator verified
        encoded = borsh_encode_u8(encoded, has_2 ? 50 : 100); // creator_share
        if (has_2)  {
            encoded = borsh_encode_pubkey(encoded, creator_2); // creator pubkey
            encoded = borsh_encode_bool(encoded, false); // creator verified
            encoded = borsh_encode_u8(encoded, 50); // creator_share
        }
        encoded = borsh_encode_pubkey(encoded, authority_key); // creator pubkey
        encoded = borsh_encode_bool(encoded, false); // creator verified
        encoded = borsh_encode_u8(encoded, 0); // creator_share
    }

    encoded = borsh_encode_option_none(encoded); // no collections

    encoded = borsh_encode_option_none(encoded); // no uses

    return borsh_encode_bool(encoded, true); // is_mutable
}


static uint64_t create_metaplex_metadata(SolPubkey *metaplex_metadata_key, SolPubkey *mint_key,
                                         SolPubkey *authority_key, SolPubkey *funding_key,
                                         uint32_t group_number, uint32_t block_number, uint16_t entry_index,
                                         uint8_t *metadata_uri, SolPubkey *creator_1, SolPubkey *creator_2,
                                         // All cross-program invocation must pass all account infos through, it's
                                         // the only sane way to cross-program invoke
                                         SolAccountInfo *transaction_accounts, int transaction_accounts_len)

{
    SolAccountMeta account_metas[] =
          // Metadata key
        { { /* pubkey */ metaplex_metadata_key, /* is_writable */ true, /* is_signer */ false },
          // Mint of token asset
          { /* pubkey */ mint_key, /* is_writable */ false, /* is_signer */ false },
          // Mint authority
          { /* pubkey */ authority_key, /* is_writable */ false, /* is_signer */ true },
          // payer
          { /* pubkey */ funding_key, /* is_writable */ true, /* is_signer */ true },
          // update authority info
          { /* pubkey */ authority_key, /* is_writable */ false, /* is_signer */ false },
          // system program
          { /* pubkey */ &((* (_Constants *) &Constants).system_program_id), /* is_writable */ false,
            /* is_signer */ false },
          // rent
          { /* pubkey */ &(Constants.rent_program_id), /* is_writable */ false, /* is_signer */ false } };
    
    // The name of the NFT will be "Shinobi LLL-MMM-NNNN" where LLL is the group number, MMM is the block number,
    // and NNNN is the entry index (+1).
    uint8_t name[7 + 1 + 3 + 1 + 3 + 1 + 4];
    sol_memcpy(name, "Shinobi", 7);
    name[7] = ' ';
    number_string(&(name[8]), group_number, 3);
    name[11] = '-';
    number_string(&(name[12]), block_number, 3);
    name[15] = '-';
    number_string(&(name[16]), entry_index + 1, 4);

    // Encoding the data for metaplex requires using Borsch serialize format, eugh.
    uint8_t data[BORSH_SIZE_U8 /* instruction code */ + METAPLEX_METADATA_DATA_SIZE];
    data[0] = 16; // instruction code 16 = CreateMetadataAccountV2
    uint8_t *data_end = encode_metaplex_metadata(&(data[BORSH_SIZE_U8]), name, sizeof(name), metadata_uri, creator_1,
                                                 creator_2, authority_key);
    
    SolInstruction instruction;

    instruction.program_id = &(Constants.metaplex_program_id);
    instruction.accounts = account_metas;
    instruction.account_len = sizeof(account_metas) / sizeof(account_metas[0]);
    instruction.data = data;
    instruction.data_len = ((uint64_t) data_end) - ((uint64_t) data);

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
    // UpdateMetadataAccountV2
    SolAccountMeta account_metas[] =
          // Metadata key
        { { /* pubkey */ &(entry->metaplex_metadata_account.address), /* is_writable */ true, /* is_signer */ false },
          // Update authority
          { /* pubkey */ &(Constants.nifty_authority_account), /* is_writable */ false, /* is_signer */ true } };

    // The data is a very simple borsh serialized format
    uint8_t data[1 /* Instruction code UpdateMetadataAccountV2 */ + 1 /* None */ + 1 /* None */ +
                 2 /* Some(true) */ + 1 /* None */];
    data[0] = 15;
    data[1] = 0;
    data[2] = 0;
    data[3] = 1;
    data[4] = 1;
    data[5] = 0;
    
    SolInstruction instruction;

    instruction.program_id = &(Constants.metaplex_program_id);
    instruction.accounts = account_metas;
    instruction.account_len = sizeof(account_metas) / sizeof(account_metas[0]);
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
    uint8_t symbol[4 + 1];
    uint8_t uri[200 + 1];

    uint32_t creator_count;
    SolPubkey creator_keys[3];

    {
        const uint8_t *data = metaplex_metadata_account->data;
        uint32_t data_len = (uint32_t) metaplex_metadata_account->data_len;
        
        // Decode name, string of at most 200 characters
        uint32_t ret = borsh_decode_string(data, data_len, sizeof(name) - 1, name);
        if (!ret) {
            return Error_InvalidMetadataValues;
        }
        data += ret, data_len -= ret;

        sol_log("Read name:");
        sol_log((const char *) name);
        
        // Decode symbol, string of at most 4 characters
        ret = borsh_decode_string(data, data_len, sizeof(symbol) - 1, symbol);
        if (!ret) {
            return Error_InvalidMetadataValues;
        }
        data += ret, data_len -= ret;
        
        sol_log("Read symbol:");
        sol_log((const char *) symbol);
        
        // Decode uri, string of at most 200 characters
        ret = borsh_decode_string(data, data_len, sizeof(uri) - 1, uri);
        if (!ret) {
            return Error_InvalidMetadataValues;
        }
        data += ret, data_len -= ret;
        
        sol_log("Read uri:");
        sol_log((const char *) uri);
        
        // Decode seller_fee_basis_points, which is ignored
        {
            uint16_t seller_fee_basis_points;
            ret = borsh_decode_u16(data, data_len, &seller_fee_basis_points);
            if (!ret) {
                return Error_InvalidMetadataValues;
            }
            data += ret, data_len -= ret;
        
            sol_log("Read seller_fee_basis_points:");
            sol_log_64(seller_fee_basis_points, 0, 0, 0, 0);
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
                sol_log("Read creator pubkey");
                sol_log_pubkey(&(creator_keys[i]));
                // Decode verified status of creator, which is ignored
                bool verified;
                ret = borsh_decode_bool(data, data_len, &verified);
                if (!ret) {
                    return Error_InvalidMetadataValues;
                }
                data += ret, data_len -= ret;
                sol_log("Read creator verified:");
                sol_log_64(verified, 0, 0, 0, 0);
                // Decode creator share, which is ignored
                uint8_t share;
                ret = borsh_decode_u8(data, data_len, &share);
                if (!ret) {
                    return Error_InvalidMetadataValues;
                }
                data += ret, data_len -= ret;
                sol_log("Read creator share:");
                sol_log_64(share, 0, 0, 0, 0);
            }
        }
        else {
            creator_count = 0;
        }
    }

    // creator_1 is always the first creator
    SolPubkey *creator_1 = &(creator_keys[0]);
    SolPubkey *creator_2 = &(creator_keys[1]);
    if (creator_count == 2) {
        // creator_2 is an empty pubkey if there are only 2 creators
        sol_memset(&(creator_keys[1]), 0, sizeof(creator_keys[1]));
    }

    // Encoding the data for metaplex requires using Borsch serialize format, eugh.
    uint8_t data[BORSH_SIZE_U8 /* instruction code */ + METAPLEX_METADATA_DATA_SIZE];
    data[0] = 15; // instruction code 15 = UpdateMetadataAccountV2
    uint8_t *data_end = encode_metaplex_metadata(&(data[BORSH_SIZE_U8]), name, sizeof(name), uri, creator_1,
                                                 creator_2, &(Constants.nifty_authority_account));
    
    // UpdateMetadataAccountV2
    SolAccountMeta account_metas[] =
          // Metadata key
        { { /* pubkey */ &(entry->metaplex_metadata_account.address), /* is_writable */ true, /* is_signer */ false },
          // Update authority
          { /* pubkey */ &(Constants.nifty_authority_account), /* is_writable */ false, /* is_signer */ true } };

    SolInstruction instruction;

    instruction.program_id = &(Constants.metaplex_program_id);
    instruction.accounts = account_metas;
    instruction.account_len = sizeof(account_metas) / sizeof(account_metas[0]);
    instruction.data = data;
    instruction.data_len = ((uint64_t) data_end) - ((uint64_t) data);

    // Must invoke with signed authority account
    uint8_t *seed_bytes = (uint8_t *) Constants.nifty_authority_seed_bytes;
    SolSignerSeed seed = { seed_bytes, sizeof(Constants.nifty_authority_seed_bytes) };
    SolSignerSeeds signer_seeds = { &seed, 1 };
    
    return sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
}
