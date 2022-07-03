#pragma once

#include "solana_sdk.h"

#include "inc/constants.h"
#include "inc/program_config.h"
#include "util/util_rent.c"
#include "util/util_transfer_lamports.c"


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


static bool compute_bid_address(SolPubkey *bidder, uint32_t group_number, uint32_t block_number, uint16_t entry_index,
                                uint8_t bump_seed, SolPubkey *fill_in)
{
    uint8_t prefix = PDA_Account_Seed_Prefix_Bid;

    SolSignerSeed seeds[] = { { &prefix, sizeof(prefix) },
                              { (uint8_t *) bidder, sizeof(*bidder) },
                              { (uint8_t *) &group_number, sizeof(group_number) },
                              { (uint8_t *) &block_number, sizeof(block_number) },
                              { (uint8_t *) &entry_index, sizeof(entry_index) },
                              { &bump_seed, sizeof(bump_seed) } };

    return !sol_create_program_address(seeds, sizeof(seeds) / sizeof(seeds[0]), &(Constants.nifty_program_id), fill_in);
}


static bool is_all_zeroes(const void *data, uint32_t length)
{
    uint8_t *d = (uint8_t *) data;
    
    while (length--) {
        if (*d++) {
            return false;
        }
    }

    return true;
}


// This function is only called from contexts for which an "all zero pubkey" is considered to be "empty".
static bool is_empty_pubkey(const SolPubkey *pubkey)
{
    return is_all_zeroes(pubkey, sizeof(SolPubkey));
}


static bool is_superuser_pubkey(const SolPubkey *pubkey)
{
    return SolPubkey_same(&(Constants.superuser_pubkey), pubkey);
}


static bool is_nifty_config_account(const SolPubkey *pubkey)
{
    return SolPubkey_same(&(Constants.nifty_config_account), pubkey);
}


static bool get_admin_account_address(const SolAccountInfo *config_account, SolPubkey *fill_in)
{
    // The identity of the admin is loaded from the config account; ensure that this is the actual one true config
    // account
    if (!is_nifty_config_account(config_account->key)) {
        return false;
    }

    // The data must be correctly sized -- may be larger than, but never smaller than, the expected size
    if (config_account->data_len < sizeof(ProgramConfig)) {
        return false;
    }

    // Get a reference to the admin pubkey from the config
    ProgramConfig *config = (ProgramConfig *) (config_account->data);

    *fill_in = config->admin_pubkey;

    return true;
}


static bool is_admin_account(const SolAccountInfo *config_account, SolPubkey *pubkey)
{
    // Get a reference to the admin pubkey from the config
    SolPubkey admin_pubkey;
    if (!get_admin_account_address(config_account, &admin_pubkey)) {
        return false;
    }

    return SolPubkey_same(&admin_pubkey, pubkey);
}


static bool is_master_stake_account(const SolPubkey *pubkey)
{
    return SolPubkey_same(&(Constants.master_stake_account), pubkey);
}


static bool is_shinobi_systems_vote_account(const SolPubkey *pubkey)
{
    return SolPubkey_same(&(Constants.shinobi_systems_vote_account), pubkey);
}


static bool is_nifty_authority_account(const SolPubkey *pubkey)
{
    return SolPubkey_same(&(Constants.nifty_authority_account), pubkey);
}


static bool is_nifty_program(const SolPubkey *pubkey)
{
    return SolPubkey_same(&(Constants.nifty_program_id), pubkey);
}


static bool is_system_program(const SolPubkey *pubkey)
{
    // The system program is all zero, and thus the empty pubkey check suffices
    return is_empty_pubkey(pubkey);
}


static bool is_stake_program(const SolPubkey *pubkey)
{
    return SolPubkey_same(&(Constants.stake_program_id), pubkey);
}


static bool is_spl_token_program(const SolPubkey *pubkey)
{
    return SolPubkey_same(&(Constants.spl_token_program_id), pubkey);
}


static bool is_metaplex_metadata_program(SolPubkey *pubkey)
{
    return SolPubkey_same(&(Constants.metaplex_program_id), pubkey);
}


// To be used as data to pass to the system program when invoking Allocate
typedef struct __attribute__((__packed__))
{
    uint32_t instruction_code;
    uint64_t space;
} util_AllocateData;


// To be used as data to pass to the system program when invoking Assign
typedef struct __attribute__((__packed__))
{
    uint32_t instruction_code;
    SolPubkey owner;
} util_AssignData;


// Creates an account at a Program Derived Address, where the address is derived from the program id and a set
// of seed bytes.  This is done by:
// Transfer lamports into the new account to bring it up to rent exempt minimum (which will create the account
// with a minimum size if it doesn't exist yet)
// Allocate (which will just make sure that it's the correct size)
// Assign (which will ensure that the account is owned by the program; in case it wasn't previously)
// It must be done this way *in case someone has already created this account*, say by sending lamports to create it
// via the system program.  In that case the CreateAccount instruction would fail (because of already existing
// account).  But a Transfer + Allocate + Assign + sequence works if there is an account there already or not.
static uint64_t create_pda(SolAccountInfo *new_account, SolSignerSeed *seeds, int seeds_count,
                           SolPubkey *funding_account_key, SolPubkey *owner_account_key,
                           uint64_t funding_lamports, uint64_t space,
                           // All cross-program invocation must pass all account infos through, it's
                           // the only sane way to cross-program invoke
                           SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    SolInstruction instruction;
    
    instruction.program_id = (SolPubkey *) &(Constants.system_program_id);

    SolSignerSeeds signer_seeds = { seeds, seeds_count };

    // Compute rent exempt minimum
    uint64_t rent_minimum = get_rent_exempt_minimum(space);

    // If the account doesn't have the required number of lamports yet, transfer the required number
    if (*(new_account->lamports) < rent_minimum) {
        uint64_t ret = util_transfer_lamports(funding_account_key, new_account->key,
                                              rent_minimum - *(new_account->lamports), transaction_accounts,
                                              transaction_accounts_len);
        if (ret) {
            return ret;
        }
    }

    SolAccountMeta account_metas[] = 
          // New account
        { { /* pubkey */ new_account->key, /* is_writable */ true, /* is_signer */ true } };

    instruction.accounts = account_metas;
    instruction.account_len = sizeof(account_metas) / sizeof(account_metas[0]);

    // Allocate
    {
        util_AllocateData data = { 8, space };
        instruction.data = (uint8_t *) &data;
        instruction.data_len = sizeof(data);

        uint64_t ret = sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len,
                                         &signer_seeds, 1);
        if (ret) {
            return ret;
        }
    }

    // Assign
    {
        util_AssignData data = { 1, *owner_account_key };

        instruction.data = (uint8_t *) &data;
        instruction.data_len = sizeof(data);

        uint64_t ret = sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len,
                                         &signer_seeds, 1);
        if (ret) {
            return ret;
        }
    }

    return 0;
}


// Updates an account's data size
static void set_account_size(SolAccountInfo *account, uint64_t size)
{
    ((uint64_t *) (account->data))[-1] = size;
}
