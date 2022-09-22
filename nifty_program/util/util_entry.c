#pragma once

#include "inc/bid.h"
#include "inc/clock.h"
#include "inc/types.h"
#include "util_accounts.c"
#include "util_block.c"
#include "util_token.c"


// Returns an error if [mint_account] is not the correct account
static uint64_t create_entry_mint_account(SolAccountInfo *mint_account, const SolPubkey *block_key,
                                          uint16_t entry_index, const SolPubkey *funding_key,
                                          const SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    // Compute the mint address
    uint8_t prefix = PDA_Account_Seed_Prefix_Mint;

    uint8_t bump_seed;

    const SolSignerSeed seeds[] = { { &prefix, sizeof(prefix) },
                                    { (uint8_t *) block_key, sizeof(*block_key) },
                                    { (uint8_t *) &entry_index, sizeof(entry_index) },
                                    { &bump_seed, sizeof(bump_seed) } };

    SolPubkey pubkey;
    uint64_t ret = sol_try_find_program_address(seeds, ARRAY_LEN(seeds) - 1, &(Constants.nifty_program_pubkey),
                                                &pubkey, &bump_seed);
    if (ret) {
        return ret;
    }

    // Verify that the mint address is as expected
    if (!SolPubkey_same(&pubkey, mint_account->key)) {
        return Error_CreateAccountFailed;
    }

    return create_token_mint(mint_account, seeds, ARRAY_LEN(seeds), &(Constants.nifty_authority_pubkey), funding_key,
                             0, transaction_accounts, transaction_accounts_len);
}


// Returns an error if [mint_account] is not the correct account
static uint64_t create_entry_account(SolAccountInfo *entry_account, const SolPubkey *mint_key,
                                     const SolPubkey *funding_key, const SolAccountInfo *transaction_accounts,
                                     int transaction_accounts_len)
{
    // Compute the entry address
    uint8_t prefix = PDA_Account_Seed_Prefix_Entry;

    uint8_t bump_seed;

    const SolSignerSeed seeds[] = { { &prefix, sizeof(prefix) },
                                    { (uint8_t *) mint_key, sizeof(*mint_key) },
                                    { &bump_seed, sizeof(bump_seed) } };

    SolPubkey pubkey;
    uint64_t ret = sol_try_find_program_address(seeds, ARRAY_LEN(seeds) - 1, &(Constants.nifty_program_pubkey),
                                                &pubkey, &bump_seed);
    if (ret) {
        return ret;
    }

    // Verify that the entry address is as expected
    if (!SolPubkey_same(&pubkey, entry_account->key)) {
        return Error_CreateAccountFailed;
    }

    return create_pda(entry_account, seeds, ARRAY_LEN(seeds), funding_key, &(Constants.nifty_program_pubkey),
                      get_rent_exempt_minimum(sizeof(Entry)), sizeof(Entry), transaction_accounts,
                      transaction_accounts_len);
}


static uint64_t create_entry_token_account(SolAccountInfo *token_account, const SolPubkey *mint_key,
                                           const SolPubkey *funding_key, const SolAccountInfo *transaction_accounts,
                                           int transaction_accounts_len)
{
    // Compute the entry address
    uint8_t prefix = PDA_Account_Seed_Prefix_Token;

    uint8_t bump_seed;

    const SolSignerSeed seeds[] = { { &prefix, sizeof(prefix) },
                                    { (uint8_t *) mint_key, sizeof(*mint_key) },
                                    { &bump_seed, sizeof(bump_seed) } };

    SolPubkey pubkey;
    uint64_t ret = sol_try_find_program_address(seeds, ARRAY_LEN(seeds) - 1, &(Constants.nifty_program_pubkey),
                                                &pubkey, &bump_seed);
    if (ret) {
        return ret;
    }

    // Verify that the entry token address is as expected
    if (!SolPubkey_same(&pubkey, token_account->key)) {
        return Error_CreateAccountFailed;
    }

    // First create the token account, with owner as SPL-token program
    uint64_t funding_lamports = get_rent_exempt_minimum(sizeof(SolanaTokenProgramTokenData));

    ret = create_pda(token_account, seeds, ARRAY_LEN(seeds), funding_key, &(Constants.spl_token_program_pubkey),
                     funding_lamports, sizeof(SolanaTokenProgramTokenData), transaction_accounts,
                     transaction_accounts_len);
    if (ret) {
        return ret;
    }

    // Now initialize the token account, with owner as the authority account
    SolInstruction instruction;

    instruction.program_id = &(Constants.spl_token_program_pubkey);

    SolAccountMeta account_metas[] =
          // The account to initialize.
        { { /* pubkey */ token_account->key, /* is_writable */ true, /* is_signuer */ false },
          // The mint this account will be associated with.
          { /* pubkey */ (SolPubkey *) mint_key, /* is_writable */ false, /* is_signer */ false } };

    instruction.accounts = account_metas;
    instruction.account_len = ARRAY_LEN(account_metas);

    util_InitializeAccount3Data data = {
        /* instruction_code */ 18,
        /* owner */ Constants.nifty_authority_pubkey
    };

    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    return sol_invoke(&instruction, transaction_accounts, transaction_accounts_len);
}


static uint64_t create_entry_metaplex_metadata(const SolPubkey *metaplex_metadata_key, const SolPubkey *mint_key,
                                               const SolPubkey *funding_key, uint32_t group_number,
                                               uint32_t block_number, uint16_t entry_index, const uint8_t *uri,
                                               const SolPubkey *creator_1, const SolPubkey *creator_2,
                                               const SolAccountInfo *transaction_accounts,
                                               int transaction_accounts_len)
{
    // The name of the NFT will be "Shinobi LLL-MMM-NNNN" where LLL is the group number, MMM is the block number,
    // and NNNN is the entry index (+1).
    uint8_t name[7 + 1 + 3 + 1 + 3 + 1 + 4 + 1];
    sol_memcpy(name, "Shinobi", 7);
    name[7] = ' ';
    number_string(&(name[8]), group_number, 3);
    name[11] = '-';
    number_string(&(name[12]), block_number, 3);
    name[15] = '-';
    number_string(&(name[16]), entry_index + 1, 4);
    name[sizeof(name) - 1] = 0;

    return create_metaplex_metadata(metaplex_metadata_key, mint_key, funding_key, name, (uint8_t *) "SHIN", uri,
                                    creator_1, creator_2, transaction_accounts, transaction_accounts_len);
}


// Given an entry account, returns the validated Entry or null if the entry account is invalid in some way.
static Entry *get_validated_entry(const SolAccountInfo *entry_account)
{
    // Make sure that the entry account is owned by the nifty stakes program
    if (!is_nifty_program(entry_account->owner)) {
        return 0;
    }

    // Entry account must be the correct size
    if (entry_account->data_len != sizeof(Entry)) {
        return 0;
    }

    const Entry *entry = (Entry *) entry_account->data;

    // If the entry does not have the correct data type, then it's not an entry
    if (entry->data_type != DataType_Entry) {
        return 0;
    }

    return (Entry *) entry;
}


static Entry *get_validated_entry_of_block(const SolAccountInfo *entry_account, const SolPubkey *block_key)
{
    const Entry *entry = get_validated_entry(entry_account);

    if (entry && SolPubkey_same(&(entry->block_pubkey), block_key)) {
        return (Entry *) entry;
    }
    else {
        return 0;
    }
}


// Assumes that the block containing the Entry is complete.  If block is provided, then the pre-reveal states are
// discerned, otherwise a generic EntryState_PreReveal is returned.
static EntryState get_entry_state(const Block *block, const Entry *entry, const Clock *clock)
{
    // If the entry has been revealed ...
    if (is_all_zeroes(&(entry->reveal_sha256), sizeof(entry->reveal_sha256))) {
        // If the entry has been purchased ...
        if (entry->purchase_price_lamports) {
            // It's owned, and possibly staked
            if (is_all_zeroes(&(entry->owned.stake_account), sizeof(entry->owned.stake_account))) {
                return EntryState_Owned;
            }
            else {
                return EntryState_OwnedAndStaked;
            }
        }
        // Else the entry has not been purchased, but has been revealed.
        else {
            // If it is configured to have an auction, then it may still be in auction or post auction
            if (entry->has_auction) {
                // If the auction is still ongoing, then it's in auction
                if ((entry->reveal_timestamp + entry->duration) > clock->unix_timestamp) {
                    return EntryState_InAuction;
                }
                // Else it's no longer in auction, if it was bid on, then it's waiting to be claimed
                else if (entry->auction.highest_bid_lamports) {
                    return EntryState_WaitingToBeClaimed;
                }
                // Else it's no longer in auction but never bid on, so fall through to return the unowned state
            }
            // It's simply unowned
            return EntryState_Unowned;
        }
    }
    // It's pre-reveal; if the block is provided, then the specific pre-reveal states can be determined
    else if (block) {
        // Else if the entry's block has reached its reveal criteria, then it's waiting to be revealed
        if (is_complete_block_revealable(block, clock)) {
            // If it's not revealed, but the block has reached reveal criteria, and the entry has a purchase price,
            // then it's waiting for reveal and owned
            if (entry->purchase_price_lamports) {
                return EntryState_WaitingForRevealOwned;
            }
            // Else it's not revealed, but the block has reached reveal criteria, and the entry has a purchase price,
            // so it's waiting for reveal and unowned
            else {
                return EntryState_WaitingForRevealUnowned;
            }
        }
        // Else the entry's block has not yet met its reveal criteria; if the entry has been purchased then it's
        // pre-reveal owned
        else if (entry->purchase_price_lamports) {
            return EntryState_PreRevealOwned;
        }
        // Else the entry is not revealed, not purchased, and not yet ready to be revealed
        else {
            return EntryState_PreRevealUnowned;
        }
    }
    // It's pre-reveal but with no supplied block, the specific pre-reveal state cannot be determined
    else {
        return EntryState_PreReveal;
    }
}
