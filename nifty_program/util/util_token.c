
#ifndef UTIL_TOKEN_C
#define UTIL_TOKEN_C

#include "inc/constants.h"
#include "util/util_rent.c"
#include "util/util_token.c"


//void *memset(void *b, int c, size_t len)
//{
//    return sol_memset(b, c, len);
//}

// This is the type of data stored in a Solana token program mint account
typedef struct __attribute__((packed))
{
    /// Optional authority used to mint new tokens. The mint authority may only be provided during
    /// mint creation. If no mint authority is present then the mint has a fixed supply and no
    /// further tokens may be minted.
    uint32_t has_mint_authority;
    SolPubkey mint_authority;

    /// Total supply of tokens.
    uint64_t supply;

    /// Number of base 10 digits to the right of the decimal place.
    uint8_t decimals;

    /// Is `true` if this structure has been initialized
    bool is_initialized;

    /// Optional authority to freeze token accounts.
    uint32_t has_freeze_authority;
    SolPubkey freeze_authority;
    
} SolanaMintAccountData;


typedef enum
{
    SolanaTokenAccountState_Uninitialized  = 0,
    SolanaTokenAccountState_Initialized    = 1,
    SolanaTokenAccountState_Frozen         = 2
} SolanaTokenAccountState;

// This is the type of data stored in a Solana token program token account
typedef struct __attribute__((packed))
{
    /// The mint associated with this account
    SolPubkey mint;

    /// The owner of this account.
    SolPubkey owner;

    /// The amount of tokens this account holds.
    uint64_t amount;

    /// If `delegate` is `Some` then `delegated_amount` represents
    /// the amount authorized by the delegate
    uint32_t has_delegate;
    SolPubkey delegate;

    /// The account's state (one of the SolanaTokenAccountState values)
    uint8_t account_state;
        
    /// If has_is_native, this is a native token, and the value logs the rent-exempt reserve. An
    /// Account is required to be rent-exempt, so the value is used by the Processor to ensure that
    /// wrapped SOL accounts do not drop below this threshold.
    uint32_t has_is_native;
    uint64_t is_native;

    /// The amount delegated
    uint64_t delegated_amount;

    /// Optional authority to close the account.
    uint32_t has_close_authority;
    SolPubkey close_authority;
    
} SolanaTokenProgramTokenData;


typedef struct __attribute__((packed))
{
    uint8_t instruction_code; // 20 for InitializeMint2

    uint8_t decimals;

    SolPubkey mint_authority;

    uint8_t has_freeze_authority;
    SolPubkey freeze_authority;
    
} util_InitializeMint2Data;


typedef struct __attribute__((packed))
{
    uint8_t instruction_code; // 18 for InitializeAccount3

    SolPubkey owner;
    
} util_InitializeAccount3Data;


typedef struct __attribute__((packed))
{
    uint8_t instruction_code; // 7 for MintTo

    uint64_t amount;
    
} util_MintToData;


typedef struct __attribute__((packed))
{
    uint8_t instruction_code; // 6 for SetAuthority

    uint32_t authority_type; // 0 for MintTokens

    uint8_t has_new_authority;
    SolPubkey new_authority;
    
} util_SetAuthorityData;


typedef struct __attribute__((packed))
{
    uint64_t amount;
} util_TransferData;

static uint64_t create_token_mint(SolPubkey *mint_key, SolPubkey *authority_key, SolPubkey *funding_key,
                                  uint32_t group_number, uint32_t block_number, uint16_t entry_index, uint8_t bump_seed,
                                  SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    // First create the mint account, with owner as solana token program
    {
        uint64_t funding_lamports = get_rent_exempt_minimum(sizeof(SolanaMintAccountData));

        uint8_t prefix = PDA_Account_Seed_Prefix_Mint;
    
        SolSignerSeed seeds[] = { { &prefix, sizeof(prefix) },
                                  { (uint8_t *) &group_number, sizeof(group_number) },
                                  { (uint8_t *) &block_number, sizeof(block_number) },
                                  { (uint8_t *) &entry_index, sizeof(entry_index) },
                                  { &bump_seed, sizeof(bump_seed) } };

        uint64_t result = create_pda(mint_key, seeds, sizeof(seeds) / sizeof(seeds[0]), funding_key,
                                     &(Constants.spl_token_program_id), funding_lamports, sizeof(SolanaMintAccountData),
                                     transaction_accounts, transaction_accounts_len);

        if (result) {
            return result;
        }
    }

    // Now initialize the mint account
    SolInstruction instruction;

    SolAccountMeta account_metas[] = 
        // Only account to pass to InitializeMint is the mint account to initialize
        { { /* pubkey */ mint_key, /* is_writable */ true, /* is_signer */ false } };

    util_InitializeMint2Data data = {
        /* instruction_code */ 20,
        /* decimals */ 0,
        /* mint_authority */ *authority_key,
        /* has_freeze_authority */ false
    };

    instruction.program_id = &(Constants.spl_token_program_id);
    instruction.accounts = account_metas;
    instruction.account_len = sizeof(account_metas) / sizeof(account_metas[0]);
    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    return sol_invoke(&instruction, transaction_accounts, transaction_accounts_len);
}


static uint64_t create_token_account(SolPubkey *token_key, SolPubkey *mint_key, SolPubkey *funding_key,
                                     uint32_t group_number, uint32_t block_number, uint16_t entry_index,
                                     uint8_t bump_seed, SolAccountInfo *transaction_accounts,
                                     int transaction_accounts_len)
{
    // First create the token account, with owner as solana token program
    {
        uint64_t funding_lamports = get_rent_exempt_minimum(sizeof(SolanaTokenProgramTokenData));

        uint8_t prefix = PDA_Account_Seed_Prefix_Token;
    
        SolSignerSeed seeds[] = { { &prefix, sizeof(prefix) },
                                  { (uint8_t *) &group_number, sizeof(group_number) },
                                  { (uint8_t *) &block_number, sizeof(block_number) },
                                  { (uint8_t *) &entry_index, sizeof(entry_index) },
                                  { &bump_seed, sizeof(bump_seed) } };

        uint64_t result = create_pda(token_key, seeds, sizeof(seeds) / sizeof(seeds[0]), funding_key,
                                     &(Constants.spl_token_program_id), funding_lamports,
                                     sizeof(SolanaTokenProgramTokenData), transaction_accounts,
                                     transaction_accounts_len);

        if (result) {
            return result;
        }
    }

    // Now initialize the token account
    SolInstruction instruction;

    SolAccountMeta account_metas[] = 
          // The account to initialize.
        { { /* pubkey */ token_key, /* is_writable */ true, /* is_signuer */ false },
          // The mint this account will be associated with.
          { /* pubkey */ mint_key, /* is_writable */ false, /* is_signer */ false } };

    util_InitializeAccount3Data data = {
        /* instruction_code */ 18,
        /* owner */ Constants.nifty_program_id
    };

    instruction.program_id = &(Constants.spl_token_program_id);
    instruction.accounts = account_metas;
    instruction.account_len = sizeof(account_metas) / sizeof(account_metas[0]);
    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    return sol_invoke(&instruction, transaction_accounts, transaction_accounts_len);
}


static uint64_t mint_token(SolPubkey *mint_key, SolPubkey *token_key, SolPubkey *authority_key,
                           SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    SolInstruction instruction;

    SolAccountMeta account_metas[] = 
          ///   0. `[writable]` The mint.
        { { /* pubkey */ mint_key, /* is_writable */ true, /* is_signuer */ false },
          ///   1. `[writable]` The account to mint tokens to.
          { /* pubkey */ token_key, /* is_writable */ true, /* is_signer */ false },
          ///   2. `[signer]` The mint's minting authority.
          { /* pubkey */ authority_key, /* is_writable */ false, /* is_signer */ true } };

    util_MintToData data = {
        /* instruction_code */ 7,
        /* amount */ 1
    };

    instruction.program_id = &(Constants.spl_token_program_id);
    instruction.accounts = account_metas;
    instruction.account_len = sizeof(account_metas) / sizeof(account_metas[0]);
    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    // Must invoke with signed authority account
    uint8_t *seed_bytes = (uint8_t *) Constants.nifty_authority_seed_bytes;
    SolSignerSeed seed = { seed_bytes, sizeof(Constants.nifty_authority_seed_bytes) };
    SolSignerSeeds signer_seeds = { &seed, 1 };
    
    return sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
}


static uint64_t revoke_mint_authority(SolPubkey *mint_key, SolPubkey *authority_key,
                                      SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    
    SolInstruction instruction;

    SolAccountMeta account_metas[] = 
          ///   0. `[writable]` The mint or account to change the authority of.
        { { /* pubkey */ mint_key, /* is_writable */ true, /* is_signuer */ false },
          ///   1. `[signer]` The current authority of the mint or account.
          { /* pubkey */ authority_key, /* is_writable */ false, /* is_signer */ true } };

    util_SetAuthorityData data = {
        /* instruction_code */ 6,
        /* authority_type */ 0, // MintTokens
        /* has_new_authority */ false,
        /* new_authority */ Constants.system_program_id
    };

    instruction.program_id = &(Constants.spl_token_program_id);
    instruction.accounts = account_metas;
    instruction.account_len = sizeof(account_metas) / sizeof(account_metas[0]);
    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    // Must invoke with signed authority account
    uint8_t *seed_bytes = (uint8_t *) Constants.nifty_authority_seed_bytes;
    SolSignerSeed seed = { seed_bytes, sizeof(Constants.nifty_authority_seed_bytes) };
    SolSignerSeeds signer_seeds = { &seed, 1 };

    return sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
}


#if 0
// Transfers token to token account, making the token account if it doesn't exist
static uint64_t transfer_token(SolAccountInfo *token_source, SolAccountInfo *token_destination, SolPubkey *token_mint)
{
    // If the destination account does not exist, then create it
    if (*(token_destination->lamports) == 0) {
        
    }

    SolAccountMeta account_metas[] =
          ///   0. `[writable]` The source account.
        { { token_source->key, true, true },
          ///   1. `[writable]` The destination account.
          { token_destination->key, true, false },
          ///   2. `[signer]` The source account's owner/delegate.
          { token_source->key, true, true } };

    util_TransferData data = {
        /* amount */ 1
    };

    SolInstruction instruction;

    instruction.program_id = &(Constants.spl_token_program_id);
    instruction.accounts = account_metas;
    instruction.account_len = sizeof(account_metas) / sizeof(account_metas[0]);
    instruction.data = (uint8_t *) data;
    instruction.data_len = sizeof(data);

    
    
}
#endif                          


#endif // UTIL_TOKEN_C
