#pragma once

#include "solana_sdk.h"

#include "inc/constants.h"
#include "inc/error.h"
#include "inc/instruction_accounts.h"
#include "inc/program_config.h"
#include "inc/types.h"
#include "util/util_accounts.c"
#include "util/util_metaplex.c"
#include "util/util_rent.c"
#include "util/util_stake.c"
#include "util/util_token.c"


// Data passed to this program for Initialize function
typedef struct
{
    uint8_t instruction_code;

    SolPubkey admin_pubkey;
    
} InitializeData;


static uint64_t super_initialize(SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,   superuser_account,             ReadOnly,   Signer,     KnownAccount_SuperUser);
        DECLARE_ACCOUNT(1,   config_account,                ReadWrite,  NotSigner,  KnownAccount_ProgramConfig);
        DECLARE_ACCOUNT(2,   authority_account,             ReadWrite,  NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(3,   master_stake_account,          ReadWrite,  NotSigner,  KnownAccount_MasterStake);
        DECLARE_ACCOUNT(4,   shinobi_systems_vote_account,  ReadOnly,   NotSigner,  KnownAccount_ShinobiSystemsVote);
        DECLARE_ACCOUNT(5,   ki_mint_account,               ReadWrite,  NotSigner,  KnownAccount_KiMint);
        DECLARE_ACCOUNT(6,   ki_metadata_account,           ReadWrite,  NotSigner,  KnownAccount_KiMetadata);
        DECLARE_ACCOUNT(7,   bid_marker_mint_account,       ReadWrite,  NotSigner,  KnownAccount_BidMarkerMint);
        DECLARE_ACCOUNT(8,   bid_marker_metadata_account,   ReadWrite,  NotSigner,  KnownAccount_BidMarkerMetadata);
        DECLARE_ACCOUNT(9,   clock_sysvar_account,          ReadOnly,   NotSigner,  KnownAccount_ClockSysvar);
        DECLARE_ACCOUNT(10,  rent_sysvar_account,           ReadOnly,   NotSigner,  KnownAccount_RentSysvar);
        DECLARE_ACCOUNT(11,  stake_history_sysvar_account,  ReadOnly,   NotSigner,  KnownAccount_StakeHistorySysvar);
        DECLARE_ACCOUNT(12,  stake_config_account,          ReadOnly,   NotSigner,  KnownAccount_StakeConfig);
        DECLARE_ACCOUNT(13,  system_program_account,        ReadOnly,   NotSigner,  KnownAccount_SystemProgram);
        DECLARE_ACCOUNT(14,  stake_program_account,         ReadOnly,   NotSigner,  KnownAccount_StakeProgram);
        DECLARE_ACCOUNT(15,  spl_token_program_account,     ReadOnly,   NotSigner,  KnownAccount_SPLTokenProgram);
        DECLARE_ACCOUNT(16,  metaplex_program_account,      ReadOnly,   NotSigner,  KnownAccount_MetaplexProgram);
    }
    DECLARE_ACCOUNTS_NUMBER(17);

    // Ensure that the input data is the correct size
    if (params->data_len != sizeof(InitializeData)) {
        return Error_InvalidDataSize;
    }

    InitializeData *data = (InitializeData *) params->data;
    
    // If the config account already exists and with the correct owner, then fail, because can't re-create the
    // config account, can only modify it after it's created
    if ((config_account->data_len > 0) && is_nifty_program(config_account->owner)) {
        return Error_InvalidAccount_First + 1;
    }
    
    // Create the config account.  The config account is derived from a fixed seed.
    {
        uint8_t *seed_bytes = (uint8_t *) Constants.nifty_config_seed_bytes;
        SolSignerSeed seed = { seed_bytes, sizeof(Constants.nifty_config_seed_bytes) };

        if (create_pda(config_account, &seed, 1, superuser_account->key, (SolPubkey *) params->program_id,
                       get_rent_exempt_minimum(sizeof(ProgramConfig)), sizeof(ProgramConfig), params->ka,
                       params->ka_num)) {
            return Error_CreateAccountFailed;
        }

        // Set the initial contents of the config account.
        ProgramConfig *config = (ProgramConfig *) (config_account->data);
        config->data_type = DataType_ProgramConfig;
        config->admin_pubkey = data->admin_pubkey;
    }

    // Create the authority account.  The authority account is derived from a fixed seed and doesn't hold any data.
    {
        uint8_t *seed_bytes = (uint8_t *) Constants.nifty_authority_seed_bytes;
        SolSignerSeed seed = { seed_bytes, sizeof(Constants.nifty_authority_seed_bytes) };
        
        if (create_pda(authority_account, &seed, 1, superuser_account->key, &(Constants.nifty_program_pubkey),
                       get_rent_exempt_minimum(0), 0, params->ka, params->ka_num)) {
            return Error_CreateAccountFailed;
        }
    }

    // Create the master stake account
    {
        uint8_t *seed_bytes = (uint8_t *) Constants.master_stake_seed_bytes;
        SolSignerSeed seed = { seed_bytes, sizeof(Constants.master_stake_seed_bytes) };

        if (create_stake_account(master_stake_account, &seed, 1, superuser_account->key,
                                 MASTER_STAKE_ACCOUNT_MIN_LAMPORTS, &(Constants.nifty_authority_pubkey),
                                 &(Constants.nifty_authority_pubkey), params->ka, params->ka_num)) {
            return Error_CreateAccountFailed;
        }
    }

    // Delegate master stake account to Shinobi Systems
    if (delegate_stake_signed(master_stake_account->key, &(Constants.shinobi_systems_vote_pubkey),
                              params->ka, params->ka_num)) {
        return Error_FailedToDelegate;
    }

    // Create the Ki mint
    {
        uint8_t *seed_bytes = (uint8_t *) Constants.ki_mint_seed_bytes;
        SolSignerSeed seed = { seed_bytes, sizeof(Constants.ki_mint_seed_bytes) };

        if (create_token_mint(ki_mint_account, &seed, 1, &(Constants.nifty_authority_pubkey), superuser_account->key,
                              1, params->ka, params->ka_num)) {
            return Error_CreateAccountFailed;
        }
    }

    // Create the metadata for the Ki mint
    uint8_t *name = (uint8_t *) KI_TOKEN_NAME;
    uint8_t *symbol = (uint8_t *) KI_TOKEN_SYMBOL;
    uint8_t *uri = (uint8_t *) KI_TOKEN_METADATA_URI;
    if (create_metaplex_metadata(&(Constants.ki_metadata_pubkey), &(Constants.ki_mint_pubkey),
                                 superuser_account->key, name, symbol, uri, &(Constants.system_program_pubkey),
                                 &(Constants.system_program_pubkey), params->ka, params->ka_num)) {
        return Error_CreateAccountFailed;
    }

    // Create the Shinobi Bid mint
    {
        uint8_t *seed_bytes = (uint8_t *) Constants.bid_marker_mint_seed_bytes;
        SolSignerSeed seed = { seed_bytes, sizeof(Constants.bid_marker_mint_seed_bytes) };

        if (create_token_mint(bid_marker_mint_account, &seed, 1, &(Constants.nifty_authority_pubkey),
                              superuser_account->key, 1, params->ka, params->ka_num)) {
            return Error_CreateAccountFailed;
        }
    }

    // Create the metadata for the Bid Marker mint
    name = (uint8_t *) BID_MARKER_TOKEN_NAME;
    symbol = (uint8_t *) BID_MARKER_TOKEN_SYMBOL;
    uri = (uint8_t *) BID_MARKER_TOKEN_METADATA_URI;
    if (create_metaplex_metadata(&(Constants.bid_marker_metadata_pubkey), &(Constants.bid_marker_mint_pubkey),
                                 superuser_account->key, name, symbol, uri, &(Constants.system_program_pubkey),
                                 &(Constants.system_program_pubkey), params->ka, params->ka_num)) {
        return Error_CreateAccountFailed;
    }
    
    return 0;
}
