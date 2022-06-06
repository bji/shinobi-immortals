
#ifndef UTIL_METAPLEX_C
#define UTIL_METAPLEX_C

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
    
    
    // Encoding the data for metaplex requires using Borsch serialize format, eugh.
    uint8_t data[/* instruction code */         BORSH_SIZE_U8 +
                 /* name */                     BORSH_SIZE_STRING(200) +
                 /* symbol "SHIN" */            BORSH_SIZE_STRING(4) +
                 /* uri */                      BORSH_SIZE_STRING(200) +
                 /* seller_fee_basis_points */  BORSH_SIZE_U16 +
                 /* (option) creator vec */     BORSH_SIZE_OPTION(BORSH_SIZE_VEC(2,
                 /*   creator pubkey */           BORSH_SIZE_PUBKEY +
                 /*   creator verified */         BORSH_SIZE_U8 +
                 /*   creator share */            BORSH_SIZE_U8)) +
                 /* (option) collections vec */ BORSH_SIZE_OPTION(BORSH_SIZE_VEC(0,
                 /*   collections verified */     BORSH_SIZE_U8 +
                 /*   collections key */          BORSH_SIZE_PUBKEY)) +
                 /* (option) uses vec */        BORSH_SIZE_OPTION(BORSH_SIZE_VEC(0,
                 /*   uses method */              BORSH_SIZE_ENUM(0) +
                 /*   uses remaining */           BORSH_SIZE_U64 +
                 /*   uses total */               BORSH_SIZE_U64)) +
                 /* is_mutable */               BORSH_SIZE_BOOL];

    // The name of the NFT will be "Shinobi LLL-MM-NNNN" where LLL is the group number, MM is the block number,
    // and NNNN is the block index (+1).
    uint8_t name[7 + 1 + 3 + 1 + 3 + 1 + 4];
    sol_memcpy(name, "Shinobi", 7);
    name[7] = ' ';
    number_string(&(name[8]), group_number, 3);
    name[11] = '-';
    number_string(&(name[12]), block_number, 3);
    name[15] = '-';
    number_string(&(name[16]), entry_index + 1, 4);

    uint8_t *encoded = borsh_encode_u8(data, 16); // instruction code 16 = CreateMetadataAccountV2

    encoded = borsh_encode_zero_terminated_string(encoded, sizeof(name), name); // name

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
        encoded = borsh_encode_u8(encoded, 0); // creator verified
        encoded = borsh_encode_u8(encoded, has_2 ? 50 : 100); // creator_share
        if (has_2)  {
            encoded = borsh_encode_pubkey(encoded, creator_2); // creator pubkey
            encoded = borsh_encode_u8(encoded, 0); // creator verified
            encoded = borsh_encode_u8(encoded, 50); // creator_share
        }
        encoded = borsh_encode_pubkey(encoded, authority_key); // creator pubkey
        encoded = borsh_encode_u8(encoded, 0); // creator verified
        encoded = borsh_encode_u8(encoded, 0); // creator_share
    }

    encoded = borsh_encode_option_none(encoded); // no collections

    encoded = borsh_encode_option_none(encoded); // no uses

    encoded = borsh_encode_bool(encoded, true); // is_mutable

    SolInstruction instruction;

    instruction.program_id = &(Constants.metaplex_program_id);
    instruction.accounts = account_metas;
    instruction.account_len = sizeof(account_metas) / sizeof(account_metas[0]);
    instruction.data = data;
    instruction.data_len = ((uint64_t) encoded) - ((uint64_t) data);

    // Must invoke with signed authority account
    uint8_t *seed_bytes = (uint8_t *) Constants.nifty_authority_seed_bytes;
    SolSignerSeed seed = { seed_bytes, sizeof(Constants.nifty_authority_seed_bytes) };
    SolSignerSeeds signer_seeds = { &seed, 1 };
    
    return sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
}


#endif // UTIL_METAPLEX_C
