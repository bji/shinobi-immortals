#pragma once

#include "inc/block.h"
#include "inc/constants.h"
#include "inc/entry.h"
#include "util/util_rent.c"
#include "util/util_token.c"


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
    uint8_t instruction_code; // 3 for Transfer

    uint64_t amount;
} util_TransferData;


typedef struct __attribute__((packed))
{
    uint8_t instruction_code; // 8 for Burn

    uint64_t amount;
} util_BurnData;


static bool is_token_account(const SolAccountInfo *token_account, const SolPubkey *mint_pubkey, uint64_t minimum_amount)
{
    // token_account must be owned by the SPL-Token program
    if (!is_spl_token_program(token_account->owner)) {
        return false;
    }

    // token_account must be an SPL-token token account
    if (token_account->data_len != sizeof(SolanaTokenProgramTokenData)) {
        return false;
    }

    const SolanaTokenProgramTokenData *data = (SolanaTokenProgramTokenData *) token_account->data;

    // token_account must be for the correct mint
    if (!SolPubkey_same(&(data->mint), mint_pubkey)) {
        return false;
    }

    // token_account must have [minimum_amount] tokens
    if (data->amount < minimum_amount) {
        return false;
    }

    // token_account must be in the Initialized state
    return (data->account_state == SolanaTokenAccountState_Initialized);
}


// Returns true only if [token_account] is a token account of the mint [mint_pubkey] owned by [token_owner_key] and
// has at least [minimum_amount] of tokens
static bool is_token_owner(const SolAccountInfo *token_account, const SolPubkey *token_owner_key,
                           const SolPubkey *mint_pubkey, uint64_t minimum_amount)
{
    // token_account must be a token account for the mint
    if (!is_token_account(token_account, mint_pubkey, minimum_amount)) {
        return false;
    }

    // token_account must be owned by token_owner_account
    return SolPubkey_same(&(((SolanaTokenProgramTokenData *) token_account->data)->owner), token_owner_key);
}


static uint64_t create_token_mint(SolAccountInfo *mint_account, const SolSignerSeed *mint_account_seeds,
                                  uint8_t mint_account_seed_count, const SolPubkey *authority_key,
                                  const SolPubkey *funding_key, uint8_t decimals,
                                  const SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    // First create the mint account, with owner as SPL-token program
    uint64_t funding_lamports = get_rent_exempt_minimum(sizeof(SolanaMintAccountData));

    uint64_t result = create_pda(mint_account, mint_account_seeds, mint_account_seed_count, funding_key,
                                 &(Constants.spl_token_program_pubkey), funding_lamports,
                                 sizeof(SolanaMintAccountData), transaction_accounts, transaction_accounts_len);
    if (result) {
        return result;
    }

    // Now initialize the mint account
    SolInstruction instruction;

    instruction.program_id = &(Constants.spl_token_program_pubkey);

    SolAccountMeta account_metas[] =
        // Only account to pass to InitializeMint2 is the mint account to initialize
        { { /* pubkey */ mint_account->key, /* is_writable */ true, /* is_signer */ false } };

    instruction.accounts = account_metas;
    instruction.account_len = ARRAY_LEN(account_metas);

    util_InitializeMint2Data data = {
        /* instruction_code */ 20,
        /* decimals */ decimals,
        /* mint_authority */ *authority_key,
        /* has_freeze_authority */ false
    };

    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    return sol_invoke(&instruction, transaction_accounts, transaction_accounts_len);
}


// When this returns, the given token account will exist for the given mint with the given owner.  Only returns
// an error if it can't make that happen.
static uint64_t create_associated_token_account_idempotent(const SolAccountInfo *token_account,
                                                           const SolPubkey *mint_key, const SolPubkey *owner_key,
                                                           const SolPubkey *funding_key,
                                                           const SolAccountInfo *transaction_accounts,
                                                           int transaction_accounts_len)
{
    // If the token account already existed, then if it isn't owned by the system account, then it must be a token
    // account for the given mint and owned by owner_key.
    if ((*(token_account->lamports) > 0) &&
        !is_system_program(token_account->owner) &&
        !is_token_owner(token_account, owner_key, mint_key, 0)) {
        return Error_InvalidTokenAccount;
    }

    // Otherwise the account didn't exist, or was owned by the system program, so create it using the Associated Token
    // Account program to create the token account
    SolInstruction instruction;

    instruction.program_id = &(Constants.spl_associated_token_account_program_pubkey);

    SolAccountMeta account_metas[] =
          ///   0. `[writeable,signer]` Funding account (must be a system account)
        { { /* pubkey */ (SolPubkey *) funding_key, /* is_writable */ true, /* is_signer */ true },
          ///   1. `[writeable]` Associated token account address to be created
          { /* pubkey */ (SolPubkey *) token_account->key, /* is_writable */ true, /* is_signer */ false },
          ///   2. `[]` Wallet address for the new associated token account
          { /* pubkey */ (SolPubkey *) owner_key, /* is_writable */ false, /* is_signer */ false },
          ///   3. `[]` The token mint for the new associated token account
          { /* pubkey */ (SolPubkey *) mint_key, /* is_writable */ false, /* is_signer */ false },
          ///   4. `[]` System program
          { /* pubkey */ &(Constants.system_program_pubkey), /* is_writable */ false, /* is_signer */ false },
          ///   5. `[]` SPL Token program
          { /* pubkey */ &(Constants.spl_token_program_pubkey), /* is_writable */ false, /* is_signer */ false } };

    instruction.accounts = account_metas;
    instruction.account_len = ARRAY_LEN(account_metas);

    // CreateIdempotent instruction
    uint8_t one = 1;
    instruction.data = &one;
    instruction.data_len = sizeof(one);

    return sol_invoke(&instruction, transaction_accounts, transaction_accounts_len);
}


static uint64_t create_pda_token_account_idempotent(SolAccountInfo *token_account, const SolPubkey *mint_key,
                                                    const SolPubkey *owner_key, const SolPubkey *funding_key,
                                                    const SolSignerSeed *seeds, int seeds_count,
                                                    const SolAccountInfo *transaction_accounts,
                                                    int transaction_accounts_len)
{
    if (*(token_account->lamports) > 0) {
        // Make sure that this is a token account for the given mint, doesn't need to have any tokens in it though
        if (is_token_owner(token_account, owner_key, mint_key, 0)) {
            // Already exists as a valid account
            return 0;
        }

        // The account that was passed in was not the proper token account for the given account address
        return Error_InvalidTokenAccount;
    }

    // Create PDA
    uint64_t ret = create_pda(token_account, seeds, seeds_count, funding_key, &(Constants.spl_token_program_pubkey),
                              get_rent_exempt_minimum(sizeof(SolanaTokenProgramTokenData)),
                              sizeof(SolanaTokenProgramTokenData), transaction_accounts, transaction_accounts_len);
    if (ret) {
        return ret;
    }

    SolInstruction instruction;

    instruction.program_id = &(Constants.spl_token_program_pubkey);

    SolAccountMeta account_metas[] =
          ///   0. `[writable]`  The account to initialize.
        { { /* pubkey */ token_account->key, /* is_writable */ true, /* is_signer */ false },
          ///   1. `[]` The mint this account will be associated with.
          { /* pubkey */ (SolPubkey *) mint_key, /* is_writable */ false, /* is_signer */ false } };

    instruction.accounts = account_metas;
    instruction.account_len = ARRAY_LEN(account_metas);

    util_InitializeAccount3Data data =
        {
            18, // instruction_code, 18 for InitializeAccount3
            *funding_key, // owner
        };

    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    return sol_invoke(&instruction, transaction_accounts, transaction_accounts_len);
}


static uint64_t mint_tokens(const SolPubkey *mint_key, const SolPubkey *token_key, uint64_t amount,
                            const SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    SolInstruction instruction;

    instruction.program_id = &(Constants.spl_token_program_pubkey);

    SolAccountMeta account_metas[] =
          ///   0. `[writable]` The mint.
        { { /* pubkey */ (SolPubkey *) mint_key, /* is_writable */ true, /* is_signer */ false },
          ///   1. `[writable]` The account to mint tokens to.
          { /* pubkey */ (SolPubkey *) token_key, /* is_writable */ true, /* is_signer */ false },
          ///   2. `[signer]` The mint's minting authority.
          { /* pubkey */ &(Constants.authority_pubkey), /* is_writable */ false, /* is_signer */ true } };

    instruction.accounts = account_metas;
    instruction.account_len = ARRAY_LEN(account_metas);

    util_MintToData data = {
        /* instruction_code */ 7,
        /* amount */ amount
    };

    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    // Must invoke with signed authority account
    const uint8_t *seed_bytes = (uint8_t *) Constants.authority_seed_bytes;
    SolSignerSeed seed = { seed_bytes, sizeof(Constants.authority_seed_bytes) };
    SolSignerSeeds signer_seeds = { &seed, 1 };

    return sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
}


static uint64_t revoke_mint_authority(const SolPubkey *mint_key, const SolPubkey *authority_key,
                                      const SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{

    SolInstruction instruction;
    instruction.program_id = &(Constants.spl_token_program_pubkey);

    SolAccountMeta account_metas[] =
          ///   0. `[writable]` The mint or account to change the authority of.
        { { /* pubkey */ (SolPubkey *) mint_key, /* is_writable */ true, /* is_signer */ false },
          ///   1. `[signer]` The current authority of the mint or account.
          { /* pubkey */ (SolPubkey *) authority_key, /* is_writable */ false, /* is_signer */ true } };

    instruction.accounts = account_metas;
    instruction.account_len = ARRAY_LEN(account_metas);

    util_SetAuthorityData data = {
        /* instruction_code */ 6,
        /* authority_type */ 0, // MintTokens
        /* has_new_authority */ false,
        /* new_authority */ Constants.system_program_pubkey
    };

    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    // Must invoke with signed authority account
    const uint8_t *seed_bytes = Constants.authority_seed_bytes;
    SolSignerSeed seed = { seed_bytes, sizeof(Constants.authority_seed_bytes) };
    SolSignerSeeds signer_seeds = { &seed, 1 };

    return sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
}


// Transfers entry token account to a destination
static uint64_t transfer_entry_token(const Entry *entry, const SolAccountInfo *token_destination,
                                     const SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    SolInstruction instruction;

    instruction.program_id = &(Constants.spl_token_program_pubkey);

    SolAccountMeta account_metas[] =
          ///   0. `[writable]` The source account.
        { { (SolPubkey *) &(entry->token_pubkey), true, false },
          ///   1. `[writable]` The destination account.
          { (SolPubkey *) token_destination->key, /* is_writable */ true, /* is_signer */ false },
          ///   2. `[signer]` The source account's owner/delegate, which since this is a token owned by the
          //                  entry, the owner is the authority account
          { &(Constants.authority_pubkey), /* is_writable */ false, /* is_signer */ true } };

    instruction.accounts = account_metas;
    instruction.account_len = ARRAY_LEN(account_metas);

    util_TransferData data = {
        /* instruction_code */ 3,
        /* amount */ 1
    };

    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    // Must invoke with signed authority account
    const uint8_t *seed_bytes = Constants.authority_seed_bytes;
    SolSignerSeed seed = { seed_bytes, sizeof(Constants.authority_seed_bytes) };
    SolSignerSeeds signer_seeds = { &seed, 1 };

    return sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
}


static uint64_t close_token_account(const SolPubkey *token_pubkey, const SolPubkey *owner_pubkey,
                                    const SolPubkey *lamports_destination_key,
                                    const SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    SolInstruction instruction;

    instruction.program_id = &(Constants.spl_token_program_pubkey);

    SolAccountMeta account_metas[] =
          ///   0. `[writable]` The account to close.
        { { (SolPubkey *) token_pubkey, /* is_writable */ true, /* is_signer */ false },
          ///   1. `[writable]` The destination account.
          { (SolPubkey *) lamports_destination_key, /* is_writable */ true, /* is_signer */ false },
          ///   2. `[signer]` The owner/delegate of the account to close, which since this is a token owned by the
          //                  entry, the owner is the authority account
          { (SolPubkey *) owner_pubkey, /* is_writable */ false, /* is_signer */ true } };

    instruction.accounts = account_metas;
    instruction.account_len = ARRAY_LEN(account_metas);

    uint8_t data = 9; // CloseAccount

    instruction.data = &data;
    instruction.data_len = sizeof(data);

    // Must invoke with signed authority account
    const uint8_t *seed_bytes = Constants.authority_seed_bytes;
    SolSignerSeed seed = { seed_bytes, sizeof(Constants.authority_seed_bytes) };
    SolSignerSeeds signer_seeds = { &seed, 1 };

    return sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
}


// Closes an empty token account and transfers the lamports to a destination account
static uint64_t close_entry_token(const Entry *entry, const SolPubkey *lamports_destination_key,
                                  const SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    return close_token_account(&(entry->token_pubkey), &(Constants.authority_pubkey), lamports_destination_key,
                               transaction_accounts, transaction_accounts_len);
}


// Burns tokens
static uint64_t burn_tokens(const SolPubkey *token_account_key, const SolPubkey *token_owner_account_key,
                            const SolPubkey *mint_key, uint64_t to_burn,
                            const SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    SolInstruction instruction;

    instruction.program_id = &(Constants.spl_token_program_pubkey);

    SolAccountMeta account_metas[] =
          ///   0. `[writable]` The account to burn from.
        { { (SolPubkey *) token_account_key, /* is_writable */ true, /* is_signer */ false },
          ///   1. `[writable]` The token mint.
          { (SolPubkey *) mint_key, /* is_writable */ true, /* is_signer */ false },
          ///   2. `[signer]` The account's owner/delegate.
          { (SolPubkey *) token_owner_account_key, /* is_writable */ false, /* is_signer */ true } };

    instruction.accounts = account_metas;
    instruction.account_len = ARRAY_LEN(account_metas);

    util_BurnData data = {
        /* instruction code */ 8,
        /* amount */ to_burn
    };

    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    return sol_invoke(&instruction, transaction_accounts, transaction_accounts_len);
}
