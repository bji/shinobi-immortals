#pragma once

#include "util/util_stake.c"
#include "util/util_calculations.c"


static uint64_t charge_commission(const Stake *stake, const Block *block, Entry *entry,
                                  const SolPubkey *stake_account_key, uint64_t current_epoch,
                                  const SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    // If the stake account is currently activating or deactivating, then don't charge commission.  Activating or
    // deactivating stake accounts pass into this function in situations in which commission should not be charged.
    if ((stake->stake.delegation.activation_epoch >= current_epoch) ||
        (stake->stake.delegation.deactivation_epoch != UINT64_MAX)) {
        return 0;
    }

    // Compute commission to charge.  It is the commission as set in the block, times the difference between
    // the current lamports in the stake account minus the lamports that were in the stake account the last
    // time commission was charged.
    uint64_t commission_lamports =
        compute_commission(stake->stake.delegation.stake,
                           entry->owned.last_commission_charge_stake_account_lamports, entry->commission);

    // Update the entry's last_commission_charge_stake_account_lamports to the value it will hold after the commission
    // has been charged.
    entry->owned.last_commission_charge_stake_account_lamports = (stake->stake.delegation.stake - commission_lamports);

    // Update the entry's commission with the current value from the block, so that the next commission collection
    // will use it.  In this way, when a block's commission is changed, it doesn't affect any given entry until that
    // entry has had at least one commission collection.
    entry->commission = block->commission;

    // If there is commission to take, do so, by moving that number of lamports out of the stake account and into
    // the master stake account.
    if (commission_lamports > 0) {
        return move_stake_signed(stake_account_key, &(Constants.master_stake_pubkey), commission_lamports,
                                 transaction_accounts, transaction_accounts_len);
    }
    else {
        return 0;
    }
}
