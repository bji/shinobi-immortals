#pragma once

#include "util/util_stake.c"


// Checks to make sure that the destination account is a valid token account for the Ki mint and returns false
// if not, or on any other error, and true on success
static uint64_t harvest_ki(Stake *stake, Entry *entry, SolAccountInfo *destination_account,
                           SolPubkey *destination_account_owner_key, SolPubkey *funding_key,
                           SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    // Amount of Ki to harvest is the stake account earnings since the last harvest: it is the number of SOL earned
    // times the ki_factor.
    uint64_t harvest_amount =
        (((stake->stake.delegation.stake - entry->owned.last_ki_harvest_stake_account_lamports) * 
          entry->metadata.level_metadata[entry->level].ki_factor) / LAMPORTS_PER_SOL);

    // If the amount to harvest is zero, then wait to accumulate more
    if (harvest_amount == 0) {
        return;
    }
    
    // Now reduce the amount to harvest, to discourage very large Ki harvests per entry.  The amount to harvest is the
    // function:
    // x = (x - (x^4/106666^3))
    // To avoid rounding errors, this is refactored.
    uint64_t f = (harvest_amount * harvest_amount) / 106666ULL;
    
    harvest_amount = ((harvest_amount * 106666ULL) - (f * f)) / 1066666ULL;
    
    if (harvest_amount > 0) {
        // Ensure that the destination account exists
        uint64_t ret = create_associated_token_account_idempotent(destination_account, destination_account_owner_key,
                                                                  &(Constants.ki_mint_pubkey), funding_key,
                                                                  transaction_accounts, transaction_accounts_len);
        if (ret) {
            return ret;
        }
        
        // Mint the harvest_amount to the destination_account.  Because Ki tokens have a decimal place count of 1, which
        // is necessary to comply with metaplex metadata standards for fungible tokens, multiply the actual number of Ki
        // by 10.
        ret = mint_tokens(&(Constants.ki_mint_pubkey), destination_account->key, harvest_amount * 10,
                          transaction_accounts, transaction_accounts_len);
        if (ret) {
            return ret;
        }
    }

    // Update the entry's last_ki_harvest_stake_account_lamports to the new value.
    entry->owned.last_ki_harvest_stake_account_lamports = stake->stake.delegation.stake;

    return 0;
}
