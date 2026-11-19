#pragma once

#include "solana_sdk.h"

#include "inc/constants.h"
#include "inc/error.h"
#include "inc/instruction_accounts.h"
#include "inc/program_state.h"
#include "inc/types.h"
#include "util/util_accounts.c"
#include "util/util_metaplex.c"
#include "util/util_rent.c"
#include "util/util_token.c"


// Data passed to this program for Initialize function
typedef struct
{
    uint8_t instruction_code;

    SolPubkey admin_pubkey;

} InitializeData;


static uint64_t super_initialize(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,   superuser_account,             ReadWrite,  Signer,     KnownAccount_SuperUser);
        DECLARE_ACCOUNT(1,   state_account,                 ReadWrite,  NotSigner,  KnownAccount_ProgramState);
        DECLARE_ACCOUNT(2,   authority_account,             ReadWrite,  NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(3,   ki_mint_account,               ReadWrite,  NotSigner,  KnownAccount_KiMint);
        DECLARE_ACCOUNT(4,   ki_metadata_account,           ReadWrite,  NotSigner,  KnownAccount_KiMetadata);
        DECLARE_ACCOUNT(5,   bid_marker_mint_account,       ReadWrite,  NotSigner,  KnownAccount_BidMarkerMint);
        DECLARE_ACCOUNT(6,   bid_marker_metadata_account,   ReadWrite,  NotSigner,  KnownAccount_BidMarkerMetadata);
        DECLARE_ACCOUNT(7,   rent_sysvar_account,           ReadOnly,   NotSigner,  KnownAccount_RentSysvar);
        DECLARE_ACCOUNT(8,   system_program_account,        ReadOnly,   NotSigner,  KnownAccount_SystemProgram);
        DECLARE_ACCOUNT(9,   spl_token_program_account,     ReadOnly,   NotSigner,  KnownAccount_SPLTokenProgram);
        DECLARE_ACCOUNT(10,  metaplex_program_account,      ReadOnly,   NotSigner,  KnownAccount_MetaplexProgram);
    }
    DECLARE_ACCOUNTS_NUMBER(11);

    // Ensure that the input data is the correct size
    if (params->data_len != sizeof(InitializeData)) {
        return Error_InvalidDataSize;
    }

    const InitializeData *data = (InitializeData *) params->data;

    // If the program_state account already exists, then fail, because can't re-create the program_state account, can only modify it
    // after it's created
    if (state_account->data_len > 0) {
        return Error_InvalidAccount_First + 1;
    }

    // Create the program_state account.  The program_state account is derived from a fixed seed.
    {
        const uint8_t *seed_bytes = (uint8_t *) Constants.state_seed_bytes;
        SolSignerSeed seed = { seed_bytes, sizeof(Constants.state_seed_bytes) };

        uint64_t ret = create_pda(state_account, &seed, 1, superuser_account->key, &(Constants.self_program_pubkey),
                                  get_rent_exempt_minimum(sizeof(ProgramState)), sizeof(ProgramState), params->ka,
                                  params->ka_num);
        if (ret) {
            return ret;
        }

        // Set the initial contents of the program_state account.
        ProgramState *program_state = (ProgramState *) (state_account->data);
        program_state->data_type = DataType_ProgramState;
        program_state->admin_pubkey = data->admin_pubkey;
    }

    // Create the authority account.  The authority account is derived from a fixed seed.  Its data records the
    // total of the mystery purchase prices that it holds in escrow, which starts at zero.
    {
        const uint8_t *seed_bytes = (uint8_t *) Constants.authority_seed_bytes;
        SolSignerSeed seed = { seed_bytes, sizeof(Constants.authority_seed_bytes) };

        uint64_t ret = create_pda(authority_account, &seed, 1, superuser_account->key, &(Constants.self_program_pubkey),
                                  get_rent_exempt_minimum(sizeof(Authority)), sizeof(Authority), params->ka,
                                  params->ka_num);
        if (ret) {
            return ret;
        }

        // Set the initial contents of the authority account.  A newly created account is all zeroes, so the escrow
        // total is already zero.
        Authority *authority = (Authority *) (authority_account->data);
        authority->data_type = DataType_Authority;
    }

    // Create the Ki mint
    {
        const uint8_t *seed_bytes = (uint8_t *) Constants.ki_mint_seed_bytes;
        SolSignerSeed seed = { seed_bytes, sizeof(Constants.ki_mint_seed_bytes) };

        // The Ki mint has no freeze authority: Ki token accounts can never be frozen
        uint64_t ret = create_token_mint(ki_mint_account, &seed, 1, &(Constants.authority_pubkey),
                                         /* freeze_authority_key */ 0, superuser_account->key, 1, params->ka,
                                         params->ka_num);
        if (ret) {
            return ret;
        }
    }

    // Create the metadata for the Ki mint
    create_metaplex_metadata(&(Constants.ki_metadata_pubkey), &(Constants.ki_mint_pubkey), superuser_account->key,
                             (const uint8_t *) KI_TOKEN_NAME, (const uint8_t *) KI_TOKEN_SYMBOL,
                             (const uint8_t *) KI_TOKEN_METADATA_URI, &(Constants.system_program_pubkey),
                             &(Constants.system_program_pubkey), params->ka, params->ka_num);

    // Create the Shinobi Bid mint
    {
        const uint8_t *seed_bytes = (uint8_t *) Constants.bid_marker_mint_seed_bytes;
        SolSignerSeed seed = { seed_bytes, sizeof(Constants.bid_marker_mint_seed_bytes) };

        // The bid marker mint has no freeze authority: bid marker token accounts can never be frozen
        uint64_t ret = create_token_mint(bid_marker_mint_account, &seed, 1, &(Constants.authority_pubkey),
                                         /* freeze_authority_key */ 0, superuser_account->key, 1, params->ka,
                                         params->ka_num);
        if (ret) {
            return ret;
        }
    }

    // Create the metadata for the Bid Marker mint
    create_metaplex_metadata(&(Constants.bid_marker_metadata_pubkey), &(Constants.bid_marker_mint_pubkey),
                             superuser_account->key, (const uint8_t *) BID_MARKER_TOKEN_NAME,
                             (const uint8_t *) BID_MARKER_TOKEN_SYMBOL, (const uint8_t *) BID_MARKER_TOKEN_METADATA_URI,
                             &(Constants.system_program_pubkey), &(Constants.system_program_pubkey), params->ka,
                             params->ka_num);

    return 0;
}
