#pragma once

#include "util/util_accounts.c"
#include "util/util_calculations.c"
#include "util/util_stake.c"


// Returns true if [stake] is delegated to Shinobi Systems, whether that delegation is activating, active, deactivating,
// or fully deactivated.  Only staking rewards earned at Shinobi Systems earn Ki, and the delegated stake of such a stake
// account grows only by those rewards: deposits into a stake account do not change its delegated stake, and this
// program, which holds the stake account's authorities, never re-delegates, merges, splits or withdraws it except as
// anyone_delegate does (which accounts for the change; see there).  So for such a stake account, any increase of its
// delegated stake over the entry's Ki baseline is Shinobi Systems staking rewards that Ki is due on.
//
// A deactivating or deactivated stake account that is delegated to Shinobi Systems is included deliberately.  This
// program only ever deactivates a stake account that is delegated to some other validator (see user_stake), but
// anyone can deactivate stake that is delegated to Shinobi Systems with the Stake program's DeactivateDelinquent
// instruction, if the Shinobi Systems validator has been delinquent for long enough.  The rewards that the stake
// account earned at Shinobi Systems before that, including those of the epoch in which it was deactivated (during
// which it was still effective), are still owed Ki.
//
// A stake account delegated to any other validator never earns Ki.  Such a stake account can only be one that
// user_stake accepted while it was delegated elsewhere, and deactivated; it may still earn that validator's rewards
// for the epoch in which it was deactivated, and those must not be paid as Ki.
static bool is_delegated_to_shinobi_systems(const Stake *stake)
{
    // The delegation fields are only decoded, and only meaningful, for a stake account in the Stake state
    return ((stake->state == StakeState_Stake) &&
            is_shinobi_systems_vote_account(&(stake->stake.delegation.voter_pubkey)));
}


// Harvests Ki earned by [stake] into [destination_account], creating it as the associated Ki token account of
// [destination_account_owner_key] if necessary.  If the destination account is not a valid Ki token account for that
// owner, the Associated Token Account or SPL Token program's failure aborts the transaction.
static void harvest_ki(const Stake *stake, Entry *entry, const SolAccountInfo *destination_account,
                       const SolPubkey *destination_account_owner_key, const SolPubkey *funding_key,
                       uint64_t current_epoch, const SolAccountInfo *transaction_accounts,
                       int transaction_accounts_len)
{
    // Only a stake account delegated to Shinobi Systems earns Ki (see is_delegated_to_shinobi_systems).  Whether it is
    // deactivating or deactivated does not matter: what it earned at Shinobi Systems before then is still owed Ki.
    // Nothing is changed for a stake account delegated elsewhere: its Ki baseline is reset when anyone_delegate
    // delegates it to Shinobi Systems.
    if (!is_delegated_to_shinobi_systems(stake)) {
        return;
    }

    // If the stake account is still activating, then it has not earned anything since its Ki baseline was set, except
    // possibly rewards carried over by anyone_delegate (see there), which stay owed and are harvested once the stake
    // account is active.
    if (stake->stake.delegation.activation_epoch >= current_epoch) {
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
