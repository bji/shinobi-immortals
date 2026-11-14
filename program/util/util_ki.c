#pragma once

#include "util/util_calculations.c"
#include "util/util_stake.c"


// Checks to make sure that the destination account is a valid token account for the Ki mint and returns 0 on
// success and a non-zero Error code on failure.
static uint64_t harvest_ki(const Stake *stake, Entry *entry, const SolAccountInfo *destination_account,
                           const SolPubkey *destination_account_owner_key, const SolPubkey *funding_key,
                           uint64_t current_epoch, const SolAccountInfo *transaction_accounts,
                           int transaction_accounts_len)
{
    // If the stake account is currently activating or deactivating, then don't harvest Ki.  Activating or
    // deactivating stake accounts pass into this function in situations in which Ki should not be harvested.
    if ((stake->stake.delegation.activation_epoch >= current_epoch) ||
        (stake->stake.delegation.deactivation_epoch != UINT64_MAX)) {
        return 0;
    }

    // A decrease in delegated stake is not earnings. Reset the baseline rather than allowing unsigned subtraction to
    // turn that decrease into a reward.
    if (stake->stake.delegation.stake < entry->owned.last_ki_harvest_stake_account_lamports) {
        entry->owned.last_ki_harvest_stake_account_lamports = stake->stake.delegation.stake;
        return 0;
    }

    // Keep track of overflow; an unrepresentable harvest must not mint tokens.
    bool overflow = false;

    // Amount of Ki to harvest is the stake account earnings since the last harvest: it is the number of SOL earned
    // times the ki_factor.
    uint64_t harvest_amount =
        (checked_multiply(stake->stake.delegation.stake - entry->owned.last_ki_harvest_stake_account_lamports,
                          entry->metadata.level_metadata[entry->level].ki_factor, &overflow) / LAMPORTS_PER_SOL);

    // If there is potentially Ki to harvest, then harvest it
    if (harvest_amount > 0) {
        // Scale earnings by the effective-to-actual total stake ratio. The curve limits effective stake to 500 SOL,
        // not Ki per harvest.
        harvest_amount = compute_ki_harvest(harvest_amount, stake->stake.delegation.stake, &overflow);

        // Only if the amount to harvest is > 0, and only if an overflow didn't occur when computing it, is the
        // harvest of tokens performed
        if ((harvest_amount > 0) && !overflow) {
            // Ensure that the destination account exists
            uint64_t ret = create_associated_token_account_idempotent(destination_account, &(Constants.ki_mint_pubkey),
                                                                      destination_account_owner_key, funding_key,
                                                                      transaction_accounts, transaction_accounts_len);
            if (ret) {
                return ret;
            }

            // Mint the harvest_amount to the destination_account.
            ret = mint_tokens(&(Constants.ki_mint_pubkey), destination_account->key, harvest_amount,
                              transaction_accounts, transaction_accounts_len);
            if (ret) {
                return ret;
            }
        }

        // Update the entry's last_ki_harvest_stake_account_lamports to the new value.
        entry->owned.last_ki_harvest_stake_account_lamports = stake->stake.delegation.stake;
    }

    return 0;
}
