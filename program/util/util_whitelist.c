#pragma once

#include "inc/constants.h"
#include "inc/data_type.h"
#include "inc/whitelist.h"
#include "util/util_accounts.c"
#include "util/util_block.c"
#include "util/util_rent.c"


static Whitelist *get_validated_whitelist(const SolAccountInfo *whitelist_account)
{
    // Make sure that the whitelist account is owned by the program
    if (!is_self_program(whitelist_account->owner)) {
        return 0;
    }

    // Whitelist account must be the correct size
    if (whitelist_account->data_len != sizeof(Whitelist)) {
        return 0;
    }

    const Whitelist *whitelist = (Whitelist *) whitelist_account->data;

    // If the whitelist does not have the correct data type, then this is an error
    if (whitelist->data_type != DataType_Whitelist) {
        return 0;
    }

    return (Whitelist *) whitelist;
}


// Computes the address of the whitelist account of the block at [block_key] into [fill_in].  Returns false if the
// address could not be computed.
static bool compute_whitelist_address(const SolPubkey *block_key, SolPubkey *fill_in)
{
    uint8_t prefix = PDA_Account_Seed_Prefix_Whitelist;

    uint8_t bump_seed;

    SolSignerSeed seeds[] = { { &prefix, sizeof(prefix) },
                              { (uint8_t *) block_key, sizeof(*block_key) } };

    return (sol_try_find_program_address(seeds, ARRAY_LEN(seeds), &(Constants.self_program_pubkey), fill_in,
                                         &bump_seed) == 0);
}


// If the block already exists, this function will always return an error.
// Adds pubkeys to the whitelist for a block.  Creates the whitelist account if it doesn't yet exist.
static uint64_t add_whitelist_entries(SolAccountInfo *whitelist_account, const SolAccountInfo *block_account,
                                      const SolPubkey *funding_pubkey, uint16_t whitelisted_pubkey_count,
                                      const SolPubkey *whitelisted_pubkeys, const SolAccountInfo *transaction_accounts,
                                      int transaction_accounts_len)
{
    // Verify that the block account does not exist.  This is necessary because whitelists cannot be created after a
    // block is created.  This ensures that whitelists are not added to while sales are ongoing.
    if (get_validated_block(block_account)) {
        return Error_BlockAlreadyExists;
    }

    // Compute the whitelist address
    uint8_t prefix = PDA_Account_Seed_Prefix_Whitelist;

    uint8_t bump_seed;

    SolSignerSeed seeds[] = { { &prefix, sizeof(prefix) },
                              { (uint8_t *) block_account->key, sizeof(*(block_account->key)) },
                              { &bump_seed, sizeof(bump_seed) } };

    SolPubkey pubkey;
    uint64_t ret = sol_try_find_program_address(seeds, ARRAY_LEN(seeds) - 1, &(Constants.self_program_pubkey),
                                                &pubkey, &bump_seed);
    if (ret) {
        return ret;
    }

    // Verify that the whitelist account address is as expected
    if (!SolPubkey_same(&pubkey, whitelist_account->key)) {
        return Error_NotWhitelistAccount;
    }

    // Get the pre-existing whitelist
    Whitelist *whitelist = get_validated_whitelist(whitelist_account);

    if (whitelist == 0) {
        // No whitelist existed so create it
        ret = create_pda(whitelist_account, seeds, ARRAY_LEN(seeds), funding_pubkey, &(Constants.self_program_pubkey),
                         get_rent_exempt_minimum(sizeof(Whitelist)), sizeof(Whitelist), transaction_accounts,
                         transaction_accounts_len);
        if (ret) {
            return ret;
        }

        whitelist = (Whitelist *) whitelist_account->data;

        whitelist->data_type = DataType_Whitelist;
    }

    // Make sure they will all fit
    if ((whitelisted_pubkey_count + whitelist->count) > MAX_WHITELIST_ENTRIES) {
        return Error_TooManyWhitelistEntries;
    }

    // Copy them in
    sol_memcpy(&(whitelist->entries[whitelist->count]), whitelisted_pubkeys,
               sizeof(SolPubkey) * whitelisted_pubkey_count);

    // Count is increased
    whitelist->count += whitelisted_pubkey_count;

    return 0;
}


// Checks the ensure that the given system account address is stored in the whitelist for the given block.  If the
// block has no whitelist, then this check succeeds.  Otherwise, if the account is in the whitelist, its first entry
// in the whitelist is cleared and true is returned, otherwise false is returned.
static bool whitelist_check(SolAccountInfo *whitelist_account, const SolPubkey *block_address,
                            const SolPubkey *system_account_address)
{
    // Compute the whitelist address
    uint8_t prefix = PDA_Account_Seed_Prefix_Whitelist;

    uint8_t bump_seed;

    SolSignerSeed seeds[] = { { &prefix, sizeof(prefix) },
                              { (uint8_t *) block_address, sizeof(*block_address) },
                              { &bump_seed, sizeof(bump_seed) } };

    SolPubkey pubkey;
    uint64_t ret = sol_try_find_program_address(seeds, ARRAY_LEN(seeds) - 1, &(Constants.self_program_pubkey),
                                                &pubkey, &bump_seed);
    if (ret) {
        return false;
    }

    // Verify that the whitelist account address is as expected
    if (!SolPubkey_same(&pubkey, whitelist_account->key)) {
        return false;
    }

    // Get the whitelist
    Whitelist *whitelist = get_validated_whitelist(whitelist_account);

    // If no valid whitelist, or no entries in the whitelist, then the check implicitly succeeds
    if (!whitelist || (whitelist->count == 0)) {
        return true;
    }

    // Find the system_account_address in the whitelist
    for (uint16_t i = 0; i < whitelist->count; i++) {
        // This is the pubkey in the whitelist at the whitelist_index
        SolPubkey *whitelist_entry = &(whitelist->entries[i]);
        // If this matches, then if there are more entries, copy the last one over top of the found one, zero out
        // the last one, and reduce the count by 1
        if (SolPubkey_same(system_account_address, whitelist_entry)) {
            SolPubkey *last_entry = &(whitelist->entries[whitelist->count - 1]);
            *whitelist_entry = *last_entry;
            *last_entry = Constants.system_program_pubkey; // All zeroes
            whitelist->count -= 1;
            return true;
        }
    }

    // Did not find the system_account_address in the whitelist
    return false;
}


// Deletes a whitelist, returning the lamports in it to the destination account.  An empty whitelist can always be
// deleted, as can any whitelist whose block has not been created yet.  A non-empty whitelist whose block exists and
// uses a whitelist (whitelist_duration > 0) cannot be deleted until the block's whitelist period is over: neither
// while the block's entries are still being added, nor during the whitelist period itself.  See the comment on
// whitelist_duration in inc/block.h.
static uint64_t delete_whitelist_account(SolAccountInfo *whitelist_account, const SolAccountInfo *block_account,
                                         const Clock *clock, SolAccountInfo *destination_account,
                                         const SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    // Compute the whitelist address
    uint8_t prefix = PDA_Account_Seed_Prefix_Whitelist;

    uint8_t bump_seed;

    SolSignerSeed seeds[] = { { &prefix, sizeof(prefix) },
                              { (uint8_t *) block_account->key, sizeof(*(block_account->key)) },
                              { &bump_seed, sizeof(bump_seed) } };

    SolPubkey pubkey;
    uint64_t ret = sol_try_find_program_address(seeds, ARRAY_LEN(seeds) - 1, &(Constants.self_program_pubkey),
                                                &pubkey, &bump_seed);
    if (ret) {
        return Error_NotWhitelistAccount;
    }

    // Verify that the whitelist account address is as expected
    if (!SolPubkey_same(&pubkey, whitelist_account->key)) {
        return Error_NotWhitelistAccount;
    }

    // If the whitelist has entries in it, then check the block to make sure that its whitelist is not in use.  A block
    // that uses a whitelist uses it from the time that the block is created: while its entries are still being added,
    // and then until whitelist_duration seconds after its last entry is added.  While the block is incomplete,
    // block_start_timestamp is still 0, so the timestamp comparison alone would wrongly treat the whitelist period as
    // already over; hence the separate check for an incomplete block.  Since a whitelist cannot be created or added to
    // once its block exists, this makes a block's whitelist fixed, and in effect, from the moment that the block is
    // created until its whitelist period is over.
    const Whitelist *whitelist = get_validated_whitelist(whitelist_account);
    if (whitelist && whitelist->count) {
        const Block *block = get_validated_block(block_account);
        if (block && (block->config.whitelist_duration > 0) &&
            (!is_block_complete(block) ||
             ((block->block_start_timestamp + block->config.whitelist_duration) > clock->unix_timestamp))) {
            return Error_WhitelistBlockInProgress;
        }
    }

    // Delete the whitelist account, transaferring its lamports to the destination account
    delete_account(whitelist_account, destination_account);

    return 0;
}
