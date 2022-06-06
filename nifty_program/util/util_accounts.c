
#ifndef UTIL_ACCOUNTS_C
#define UTIL_ACCOUNTS_C

#include "solana_sdk.h"

#include "inc/constants.h"


// This function is only called from contexts for which an "all zero pubkey" is considered to be "empty".
static bool is_empty_pubkey(SolPubkey *pubkey)
{
    for (uint8_t i = 0; i < sizeof(SolPubkey); i++) {
        if (pubkey->x[i]) {
            return false;
        }
    }

    return true;
}


static bool is_superuser_pubkey(SolPubkey *pubkey)
{
    return SolPubkey_same(&(Constants.superuser_pubkey), pubkey);
}


static bool is_nifty_config_account(SolPubkey *pubkey)
{
    return SolPubkey_same(&(Constants.nifty_config_account), pubkey);
}


static bool is_nifty_authority_account(SolPubkey *pubkey)
{
    return SolPubkey_same(&(Constants.nifty_authority_account), pubkey);
}


static bool is_nifty_program(SolPubkey *pubkey)
{
    return SolPubkey_same(&(Constants.nifty_program_id), pubkey);
}


static bool is_system_program(SolPubkey *pubkey)
{
    // The system program is all zero, and thus the empty pubkey check suffices
    return is_empty_pubkey(pubkey);
}


// To be used as data to pass to the system program when invoking CreateAccount
typedef struct __attribute__((__packed__))
{
    uint32_t instruction_code;
    uint64_t lamports;
    uint64_t space;
    SolPubkey owner;
} util_CreateAccountData;


// Creates an account at a Program Derived Address, where the address is derived from the program id and a set
// of seed bytes.
static uint64_t create_pda(SolPubkey *new_account_key, SolSignerSeed *seeds, int seeds_count,
                           SolPubkey *funding_account_key, SolPubkey *owner_account_key,
                           uint64_t funding_lamports, uint64_t space,
                           // All cross-program invocation must pass all account infos through, it's
                           // the only sane way to cross-program invoke
                           SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    SolInstruction instruction;

    SolAccountMeta account_metas[] = 
        // First account to pass to CreateAccount is the funding_account
        { { /* pubkey */ funding_account_key, /* is_writable */ true, /* is_signer */ true },
          // Second account to pass to CreateAccount is the new account to be created
          { /* pubkey */ new_account_key, /* is_writable */ true, /* is_signer */ true } };

    util_CreateAccountData data = { 0, funding_lamports, space, *owner_account_key };

    instruction.program_id = (SolPubkey *) &(Constants.system_program_id);
    instruction.accounts = account_metas;
    instruction.account_len = sizeof(account_metas) / sizeof(account_metas[0]);
    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    SolSignerSeeds signer_seeds = { seeds, seeds_count };
    
    return sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1);
}


// Updates an account's data size
static void set_account_size(SolAccountInfo *account, uint64_t size)
{
    ((uint64_t *) (account->data))[-1] = size;
}


// Computes an Entry address given given the group number, block number, entry number, and bump seed.
// This will be a PDA of the nifty stakes program.
static bool compute_entry_address(uint32_t group_number, uint32_t block_number, uint16_t entry_index,
                                  uint8_t bump_seed, SolPubkey *fill_in)

{
    uint8_t prefix = PDA_Account_Seed_Prefix_Entry;
    
    SolSignerSeed seeds[] = { { &prefix, sizeof(prefix) },
                              { (uint8_t *) &group_number, sizeof(group_number) },
                              { (uint8_t *) &block_number, sizeof(block_number) },
                              { (uint8_t *) &entry_index, sizeof(entry_index) },
                              { &bump_seed, sizeof(bump_seed) } };

    return !sol_create_program_address(seeds, sizeof(seeds) / sizeof(seeds[0]), &(Constants.nifty_program_id), fill_in);
}


// Computes the account address of a mint given the group number, block number, entry number, and bump seed.
// This will be a PDA of metaplex metadata program.
static bool compute_mint_address(uint32_t group_number, uint32_t block_number, uint16_t entry_index, uint8_t bump_seed,
                                 SolPubkey *fill_in)
{
    uint8_t prefix = PDA_Account_Seed_Prefix_Mint;
    
    SolSignerSeed seeds[] = { { &prefix, sizeof(prefix) },
                              { (uint8_t *) &group_number, sizeof(group_number) },
                              { (uint8_t *) &block_number, sizeof(block_number) },
                              { (uint8_t *) &entry_index, sizeof(entry_index) },
                              { &bump_seed, sizeof(bump_seed) } };

    return !sol_create_program_address(seeds, sizeof(seeds) / sizeof(seeds[0]), &(Constants.nifty_program_id), fill_in);
}


// Computes the account address for the token associated with a mint given the group number, block number, entry
// number, and bump seed.  This will be a PDA of nifty stakes program.
static bool compute_token_address(uint32_t group_number, uint32_t block_number, uint16_t entry_index,
                                  uint8_t bump_seed, SolPubkey *fill_in)
{
    uint8_t prefix = PDA_Account_Seed_Prefix_Token;
    
    SolSignerSeed seeds[] = { { &prefix, sizeof(prefix) },
                              { (uint8_t *) &group_number, sizeof(group_number) },
                              { (uint8_t *) &block_number, sizeof(block_number) },
                              { (uint8_t *) &entry_index, sizeof(entry_index) },
                              { &bump_seed, sizeof(bump_seed) } };

    return !sol_create_program_address(seeds, sizeof(seeds) / sizeof(seeds[0]), &(Constants.nifty_program_id), fill_in);
}


// Computes the metaplex metadata id address of a mint
// This will be a PDA of metaplex metadata program.
static bool compute_metaplex_metadata_address(SolPubkey *mint_address, uint8_t bump_seed, SolPubkey *fill_in)
{
    SolSignerSeed seeds[] = { { (const uint8_t *) "metadata", 8 },
                              { (uint8_t *) &(Constants.metaplex_program_id), sizeof(SolPubkey) },
                              { (uint8_t *) mint_address, sizeof(*mint_address) },
                              { &bump_seed, sizeof(bump_seed) } };

    return !sol_create_program_address(seeds, sizeof(seeds) / sizeof(seeds[0]), &(Constants.metaplex_program_id),
                                       fill_in);
}


// Computes the metaplex metadata id edition address of a mint
// This will be a PDA of metaplex metadata program.
static bool compute_metaplex_edition_metadata_address(SolPubkey *mint_address, uint8_t bump_seed, SolPubkey *fill_in)
{
    SolSignerSeed seeds[] = { { (const uint8_t *) "metadata", 8 },
                              { (uint8_t *) &(Constants.metaplex_program_id), sizeof(SolPubkey) },
                              { (uint8_t *) mint_address, sizeof(*mint_address) },
                              { (const uint8_t *) "edition", 7 },
                              { &bump_seed, sizeof(bump_seed) } };

    return !sol_create_program_address(seeds, sizeof(seeds) / sizeof(seeds[0]), &(Constants.metaplex_program_id),
                                       fill_in);
}


// Computes a Block address given given the group number, block number, and bump seed.
// This will be a PDA of the nifty stakes program.
static bool compute_block_address(uint32_t group_number, uint32_t block_number, uint8_t bump_seed, SolPubkey *fill_in)

{
    uint8_t prefix = PDA_Account_Seed_Prefix_Block;
    
    SolSignerSeed seeds[] = { { &prefix, sizeof(prefix) },
                              { (uint8_t *) &group_number, sizeof(group_number) },
                              { (uint8_t *) &block_number, sizeof(block_number) },
                              { &bump_seed, sizeof(bump_seed) } };

    return !sol_create_program_address(seeds, sizeof(seeds) / sizeof(seeds[0]), &(Constants.nifty_program_id), fill_in);
}


#endif // UTIL_ACCOUNTS_C
