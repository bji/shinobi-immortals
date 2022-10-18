#pragma once

#include "util/util_math.c"
#include "util/util_stake.c"


// Checks to make sure that the destination account is a valid token account for the Ki mint and returns false
// if not, or on any other error, and true on success
static uint64_t harvest_ki(const Stake *stake, Entry *entry, const SolAccountInfo *destination_account,
                           const SolPubkey *destination_account_owner_key, const SolPubkey *funding_key,
                           const SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    // Keep track of overflow.  If overflow occurs at all, then the harvest is zero.  Overflow can only occur in
    // situations where the Ki earnings were so large that they would be zero under the reduction schedule.
    bool overflow = false;

    // Amount of Ki to harvest is the stake account earnings since the last harvest: it is the number of SOL earned
    // times the ki_factor.
    uint64_t harvest_amount =
        (checked_multiply(stake->stake.delegation.stake - entry->owned.last_ki_harvest_stake_account_lamports,
                          entry->metadata.level_metadata[entry->level].ki_factor, &overflow) / LAMPORTS_PER_SOL);

    // If there is potentially Ki to harvest, then harvest it
    if (harvest_amount > 0) {
        // Reduce the amount to harvest, to discourage very large Ki harvests per entry.  The amount to harvest is the
        // function:
        // x = (x - (x^4/106666^3))
        // To avoid rounding errors, this is refactored.
        uint64_t f = checked_multiply(harvest_amount, harvest_amount, &overflow) / 106666ul;

        // harvest_amount * 10666ul cannot overflow since the original value was divided by LAMPORTS_PER_SOL
        harvest_amount = ((harvest_amount * 106666ul) - checked_multiply(f, f, &overflow)) / 1066666ul;

        // Because Ki tokens have a decimal place count of 1, which is necessary to comply with metaplex metadata
        // standards for fungible tokens, multiply the actual number of Ki by 10.
        harvest_amount = checked_multiply(harvest_amount, 10, &overflow);

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
