#pragma once

#include "util/util_stake.c"
#include "util/util_calculations.c"


// Returns true if Destake should not charge commission on [entry], because no commission charge has completed for it
// in the last DESTAKE_COMMISSION_WAIVER_EPOCHS epochs (see constants.h).  This is decided only from the epoch that
// earlier transactions recorded, never by attempting the charge, since a failed charge aborts the transaction.
//
// Whether commission is owed need not be checked: if none is owed, charging it would move nothing, so skipping the
// charge changes nothing either (Destake clears the entry's commission and Ki records right after).
static bool is_destake_commission_waived(const Entry *entry, uint64_t current_epoch)
{
    // An epoch earlier than the recorded one cannot occur; if it somehow did, do not waive commission
    if (current_epoch < entry->owned.last_commission_charge_epoch) {
        return false;
    }

    return ((current_epoch - entry->owned.last_commission_charge_epoch) >= DESTAKE_COMMISSION_WAIVER_EPOCHS);
}


// Charges commission on [stake] if it is fully active.  A failure to move the commission out of the stake account
// aborts the transaction with the stake program's error.  Every charge that completes, even one of no commission,
// records the current epoch in the entry's owned.last_commission_charge_epoch.
static void charge_commission(const Stake *stake, const Block *block, Entry *entry,
                              const SolPubkey *stake_account_key, uint64_t current_epoch,
                              const SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    // If the stake account is currently activating or deactivating, then don't charge commission.  Activating or
    // deactivating stake accounts pass into this function in situations in which commission should not be charged.
    if ((stake->stake.delegation.activation_epoch >= current_epoch) ||
        (stake->stake.delegation.deactivation_epoch != UINT64_MAX)) {
        return;
    }

    // Compute commission to charge.  compute_commission returns 0 if the delegated stake is at or below the
    // commission baseline, so commission_lamports never exceeds the delegated stake.  It is the commission as set in the block, times the difference between
    // the current lamports in the stake account minus the lamports that were in the stake account the last
    // time commission was charged.
    uint64_t commission_lamports =
        compute_commission(stake->stake.delegation.stake,
                           entry->owned.last_commission_charge_stake_account_lamports, entry->commission);

    // Update the entry's last_commission_charge_stake_account_lamports to the value it will hold after the commission
    // has been charged.
    entry->owned.last_commission_charge_stake_account_lamports = (stake->stake.delegation.stake - commission_lamports);

    // Ki is earned on staking rewards before this program's commission is taken out of them.  Moving the commission
    // out of the stake account reduces its delegated stake, so lower the Ki baseline by the same amount; otherwise
    // the next Ki harvest would see the commission as missing rewards, and the Ki earned would depend upon whether
    // the permissionless commission charge happened to run before or after the harvest.  Since both the delegated
    // stake and the Ki baseline drop by the same amount, a Ki baseline at or below the delegated stake stays so.
    //
    // The Ki baseline is set from delegated stake and so should always be far larger than any commission, which is a
    // fraction of rewards.  If it somehow were not, do not let the subtraction wrap, and do not set the baseline to
    // zero either, since that would make the entire stake look like rewards to the next harvest.  Instead, treat the
    // rewards as already harvested by setting the baseline to the stake that remains after the commission, just as
    // harvest_ki does when it finds the stake below the baseline.
    if (entry->owned.last_ki_harvest_stake_account_lamports >= commission_lamports) {
        entry->owned.last_ki_harvest_stake_account_lamports -= commission_lamports;
    }
    else {
        entry->owned.last_ki_harvest_stake_account_lamports = (stake->stake.delegation.stake - commission_lamports);
    }

    // Update the entry's commission with the current value from the block, so that the next commission collection
    // will use it.  In this way, when a block's commission is changed, it doesn't affect any given entry until that
    // entry has had at least one commission collection.
    entry->commission = block->commission;

    // Record that a commission charge has completed in this epoch, even if no commission was owed.  If the MoveStake
    // below fails, the whole transaction aborts, and this is not recorded.  Destake uses this to stop charging
    // commission once charges have not been possible for DESTAKE_COMMISSION_WAIVER_EPOCHS (see constants.h).
    entry->owned.last_commission_charge_epoch = current_epoch;

    // If there is commission to take, do so, by moving that number of lamports out of the stake account and into
    // the master stake account.
    if (commission_lamports > 0) {
        move_stake_signed(stake_account_key, &(Constants.master_stake_pubkey), commission_lamports,
                          transaction_accounts, transaction_accounts_len);
    }
}
