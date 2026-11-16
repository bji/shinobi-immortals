#pragma once

#include "util/util_calculations.c"
#include "util/util_stake.c"


// Harvests Ki earned by [stake] into [destination_account], creating it as the associated Ki token account of
// [destination_account_owner_key] if necessary.  If the destination account is not a valid Ki token account for that
// owner, the Associated Token Account or SPL Token program's failure aborts the transaction.
static void harvest_ki(const Stake *stake, Entry *entry, const SolAccountInfo *destination_account,
                       const SolPubkey *destination_account_owner_key, const SolPubkey *funding_key,
                       uint64_t current_epoch, const SolAccountInfo *transaction_accounts,
                       int transaction_accounts_len)
{
    // If the stake account is currently activating or deactivating, then don't harvest Ki.  Activating or
    // deactivating stake accounts pass into this function in situations in which Ki should not be harvested.
    if ((stake->stake.delegation.activation_epoch >= current_epoch) ||
        (stake->stake.delegation.deactivation_epoch != UINT64_MAX)) {
        return;
    }

    // A decrease in delegated stake is not earnings. Reset the baseline rather than allowing unsigned subtraction to
    // turn that decrease into a reward.
    if (stake->stake.delegation.stake < entry->owned.last_ki_harvest_stake_account_lamports) {
        entry->owned.last_ki_harvest_stake_account_lamports = stake->stake.delegation.stake;
        return;
    }

    // Apply the effective-stake curve to earned lamports.  The baseline comparison above guarantees earnings <= the
    // current stake, bounding effective earnings to less than 60 SOL even at uint64_t limits (50 SOL ceiling plus a
    // conservative allowance for stake-input rounding).
    uint64_t actual_earnings_lamports =
        stake->stake.delegation.stake - entry->owned.last_ki_harvest_stake_account_lamports;
    uint64_t effective_earnings_lamports =
        compute_effective_stake_earnings(actual_earnings_lamports, stake->stake.delegation.stake);

    // Convert effective SOL earnings to whole Ki
    uint64_t harvest_amount = compute_ki_harvest(effective_earnings_lamports,
                                                 entry->metadata.level_metadata[entry->level].ki_factor);
    if (!harvest_amount) {
        // Leave the baseline unchanged so a sub-Ki reward can accumulate.
        return;
    }

    create_associated_token_account_idempotent(destination_account, &(Constants.ki_mint_pubkey),
                                               destination_account_owner_key, funding_key,
                                               transaction_accounts, transaction_accounts_len);

    // Only the SPL Token boundary uses base units: ten base units equal one Ki.  Effective earnings < 60 SOL and a
    // uint32_t factor bound whole Ki below 60 * UINT32_MAX; multiplying that by ten also fits comfortably in
    // uint64_t.
    uint64_t mint_amount = harvest_amount * 10;
    mint_tokens(&(Constants.ki_mint_pubkey), destination_account->key, mint_amount,
                transaction_accounts, transaction_accounts_len);

    // A successful positive harvest consumes the processed rewards
    entry->owned.last_ki_harvest_stake_account_lamports = stake->stake.delegation.stake;
}
