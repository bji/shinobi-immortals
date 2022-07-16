#pragma once

#include "inc/constants.h"

typedef enum
{
    KnownAccount_NotKnown,
    KnownAccount_SuperUser,
    KnownAccount_ProgramConfig,
    KnownAccount_Authority,
    KnownAccount_MasterStake,
    KnownAccount_KiMint,
    KnownAccount_KiMetadata,
    KnownAccount_BidMarkerMint,
    KnownAccount_BidMarkerMetadata,
    KnownAccount_ShinobiSystemsVote,
    KnownAccount_NiftyProgram,
    KnownAccount_SystemProgram,
    KnownAccount_MetaplexProgram,
    KnownAccount_SPLTokenProgram,
    KnownAccount_SPLATAProgram,
    KnownAccount_StakeProgram,
    KnownAccount_ClockSysvar,
    KnownAccount_RentSysvar,
    KnownAccount_StakeHistorySysvar,
    KnownAccount_StakeConfig
} KnownAccount;

typedef enum
{
    ReadOnly = 0,
    ReadWrite = 1
} AccountWriteable;

typedef enum
{
    NotSigner = 0,
    Signer = 1
} AccountSigner;

#define DECLARE_ACCOUNTS uint8_t _account_num = 0;

#define DECLARE_ACCOUNT(n, name, writable, signer, known_account) }                                                    \
    if (_account_num == params->ka_num) {                                                                              \
        return Error_IncorrectNumberOfAccounts;                                                                        \
    }                                                                                                                  \
    SolAccountInfo *name = &(params->ka[_account_num++]);                                                              \
    do {                                                                                                               \
        if (((writable == ReadWrite) && !name->is_writable) ||                                                         \
            ((signer == Signer) && !name->is_signer)) {                                                                \
            return Error_InvalidAccountPermissions_First + (_account_num - 1);                                         \
        }                                                                                                              \
        if (!check_known_account(name, (known_account))) {                                                             \
            return Error_InvalidAccount_First + (_account_num - 1);                                                    \
        }                                                                                                              \
    } while (0); {

#define DECLARE_ACCOUNTS_NUMBER(n) if (params->ka_num != (n)) { return Error_IncorrectNumberOfAccounts; }

        

static bool check_known_account(SolAccountInfo *account, KnownAccount known_account)
{
    SolPubkey *known_pubkey;
    
    switch (known_account) {
    case KnownAccount_NotKnown:
        return true;
        
    case KnownAccount_SuperUser:
        known_pubkey = &(Constants.superuser_pubkey);
        break;
        
    case KnownAccount_ProgramConfig:
        known_pubkey = &(Constants.nifty_config_pubkey);
        break;
        
    case KnownAccount_Authority:
        known_pubkey = &(Constants.nifty_authority_pubkey);
        break;
        
    case KnownAccount_MasterStake:
        known_pubkey = &(Constants.master_stake_pubkey);
        break;
        
    case KnownAccount_KiMint:
        known_pubkey = &(Constants.ki_mint_pubkey);
        break;
        
    case KnownAccount_KiMetadata:
        known_pubkey = &(Constants.ki_metadata_pubkey);
        break;
        
    case KnownAccount_BidMarkerMint:
        known_pubkey = &(Constants.bid_marker_mint_pubkey);
        break;
        
    case KnownAccount_BidMarkerMetadata:
        known_pubkey = &(Constants.bid_marker_metadata_pubkey);
        break;
        
    case KnownAccount_ShinobiSystemsVote:
        known_pubkey = &(Constants.shinobi_systems_vote_pubkey);
        break;
        
    case KnownAccount_NiftyProgram:
        known_pubkey = &(Constants.nifty_program_pubkey);
        break;
        
    case KnownAccount_SystemProgram:
        known_pubkey = &(Constants.system_program_pubkey);
        break;
        
    case KnownAccount_MetaplexProgram:
        known_pubkey = &(Constants.metaplex_program_pubkey);
        break;
        
    case KnownAccount_SPLTokenProgram:
        known_pubkey = &(Constants.spl_token_program_pubkey);
        break;
        
    case KnownAccount_SPLATAProgram:
        known_pubkey = &(Constants.spl_associated_token_account_program_pubkey);
        break;
        
    case KnownAccount_StakeProgram:
        known_pubkey = &(Constants.stake_program_pubkey);
        break;
        
    case KnownAccount_ClockSysvar:
        known_pubkey = &(Constants.clock_sysvar_pubkey);
        break;
        
    case KnownAccount_RentSysvar:
        known_pubkey = &(Constants.rent_sysvar_pubkey);
        break;
        
    case KnownAccount_StakeHistorySysvar:
        known_pubkey = &(Constants.stake_history_sysvar_pubkey);
        break;
        
    case KnownAccount_StakeConfig:
        known_pubkey = &(Constants.stake_config_pubkey);
        break;
    }

    return SolPubkey_same(account->key, known_pubkey);
}
