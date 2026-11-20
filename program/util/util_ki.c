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


// Brings entry->epochs_staked up to date, given [stake], the stake account that the entry holds, as it is now.  The
// epochs counted are those from entry->epochs_staked_counted_to_epoch up to but not including current_epoch (only
// completed epochs are counted), in which the stake account's delegation to Shinobi Systems was effective.  A
// delegation is effective from the epoch after its activation epoch (the epoch in which it was delegated), through its
// deactivation epoch (during which it is still effective, and earns rewards; it is inactive from the next epoch).
// Warmup and cooldown rate limits are ignored: a delegation is treated as wholly effective in every such epoch.
// Afterwards, entry->epochs_staked_counted_to_epoch is current_epoch, so that no epoch is ever counted twice.
//
// This must be called with the stake account as it was throughout the epochs being counted, before anything changes
// its delegation; anyone_delegate, which re-delegates it, calls this first.  The stake account's delegation can
// otherwise only change while this program holds it through the Stake program's DeactivateDelinquent instruction,
// which only sets its deactivation epoch, which this then accounts for.
static void count_epochs_staked(const Stake *stake, Entry *entry, uint64_t current_epoch)
{
    // The first epoch not yet considered
    uint64_t first_epoch = entry->epochs_staked_counted_to_epoch;

    // Only a delegation to Shinobi Systems counts (see is_delegated_to_shinobi_systems)
    if (is_delegated_to_shinobi_systems(stake)) {
        // The first epoch in which the delegation is effective is the one after its activation epoch.  The Stake
        // program uses an activation epoch of UINT64_MAX for stake that was active from the cluster's genesis, which
        // is effective in every epoch.
        uint64_t activation_epoch = stake->stake.delegation.activation_epoch;
        uint64_t first_effective_epoch = (activation_epoch == UINT64_MAX) ? 0 : (activation_epoch + 1);
        if (first_effective_epoch > first_epoch) {
            first_epoch = first_effective_epoch;
        }

        // The epoch after the last one to count: the current epoch, which has not completed, or else the epoch after
        // the delegation's deactivation epoch, if that is earlier.  A deactivation epoch of UINT64_MAX means that the
        // delegation has not been deactivated.
        uint64_t end_epoch = current_epoch;
        uint64_t deactivation_epoch = stake->stake.delegation.deactivation_epoch;
        if ((deactivation_epoch != UINT64_MAX) && ((deactivation_epoch + 1) < end_epoch)) {
            end_epoch = deactivation_epoch + 1;
        }

        // Count the epochs in between, if there are any.  The count is at most the number of epochs that there have
        // ever been, so the sum cannot realistically overflow; but saturate rather than wrap, to be certain.
        if (end_epoch > first_epoch) {
            uint64_t count = end_epoch - first_epoch;
            entry->epochs_staked = ((UINT64_MAX - entry->epochs_staked) < count) ?
                UINT64_MAX : (entry->epochs_staked + count);
        }
    }

    // Every epoch before the current one has now been considered.  Never move backwards.
    if (current_epoch > entry->epochs_staked_counted_to_epoch) {
        entry->epochs_staked_counted_to_epoch = current_epoch;
    }
}


// Harvests Ki earned by [stake] into [destination_account], creating it as the associated Ki token account of
// [destination_account_owner_key] if necessary.  [block] is the entry's block, which must already have been verified
// to be the entry's block; its block_start_timestamp is the beginning of the entry's life span.  [clock] is the
// current Clock sysvar, whose unix_timestamp is the end of the entry's life span.  If the destination
// account is not a valid Ki token account for that owner, the Associated Token Account or SPL Token program's failure
// aborts the transaction.
static void harvest_ki(const Stake *stake, const Block *block, Entry *entry,
                       const SolAccountInfo *destination_account,
                       const SolPubkey *destination_account_owner_key, const SolPubkey *funding_key,
                       const Clock *clock, const SolAccountInfo *transaction_accounts,
                       int transaction_accounts_len)
{
    // Every instruction that harvests Ki has the stake account at hand, so bring the count of epochs staked up to date
    // as well.  This is done first, since it does not depend on whether any Ki is harvested.
    count_epochs_staked(stake, entry, clock->epoch);

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
    if (stake->stake.delegation.activation_epoch >= clock->epoch) {
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

    // The entry's life span, in whole days: from the time at which its block was completed to the current time.  The
    // block was completed before the entry was revealed, and so before it could be staked, so the current time is
    // normally later; but Clock.unix_timestamp is an estimate that could in principle step backwards slightly, so a
    // current time earlier than the block's completion is treated as a life span of zero.
    uint64_t lifespan_days = (clock->unix_timestamp > block->block_start_timestamp) ?
        (((uint64_t) (clock->unix_timestamp - block->block_start_timestamp)) / SECONDS_PER_DAY) : 0;

    // The fraction of the harvest that the entry retains at this point in its life span.  All of the rewards being
    // harvested are paid at the current retention, however long ago they were earned.
    uint64_t retention = compute_ki_harvest_retention(entry->metadata.ki_harvest_reduction_per_day, lifespan_days);

    // Convert the retained portion of the effective SOL earnings to whole Ki
    uint64_t harvest_amount = compute_retained_ki_harvest(effective_earnings_lamports,
                                                          entry->metadata.level_metadata[entry->level].ki_factor,
                                                          retention);
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
