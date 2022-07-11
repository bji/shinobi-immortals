#pragma once

#include "util/util_stake.c"


// Checks to make sure that the destination account is a valid token account for the Ki mint and returns false
// if not, or on any other error, and true on success
static uint64_t harvest_ki(Stake *stake, Entry *entry, SolAccountInfo *destination_account,
                           SolPubkey *destination_account_owner_key, SolAccountInfo *transaction_accounts,
                           int transaction_accounts_len)
{
    // Check to make sure that the destination_account is a token account
    if (!is_token_account(destination_account, &(Constants.ki_mint_pubkey))) {
        return false;
    }

    // Amount of Ki to harvest is the stake account earnings since the last harvest, divided by this level's ki_factor
    uint64_t harvest_amount =
        ((stake->stake.delegation.stake - entry->staked.last_commission_charge_stake_account_lamports) /
         entry->metadata.level_metadata[entry->metadata.level].ki_factor);

    sol_log("Harvest Ki:");
    sol_log_64(harvest_amount, 0, 0, 0, 0);

    // If there is no ki to harvest, then the harvest is complete.
    if (harvest_amount == 0) {
        return 0;
    }

    // Ensure that the destination account exists
    uint64_t ret = create_token_account_idempotent(destination_account, destination_account_owner_key,
                                                   &(Constants.ki_mint_pubkey), destination_account_owner_key,
                                                   transaction_accounts, transaction_accounts_len);
    if (ret) {
        return ret;
    }

    // Mint the harvest_amount to the destination_account
    ret = mint_tokens(&(Constants.ki_mint_pubkey), destination_account->key, harvest_amount,
                               transaction_accounts, transaction_accounts_len);
    if (!ret) {
        return ret;
    }

    // Update the entry's last_ki_harvest_stake_account_lamports to the new value.  This is done only if Ki was
    // harvested, in case the ki_factor was just so large that no Ki could be earned this time; but maybe it can be
    // earned after there are additional stake earnings.
    entry->staked.last_ki_harvest_stake_account_lamports = stake->stake.delegation.stake;

    return 0;
}
