#pragma once

#include "util/util_stake.c"
#include "util/util_calculations.c"


// funding_account is only used to provide transient quantities of SOL for a temporary stake account
static uint64_t charge_commission(const Stake *stake, const Block *block, Entry *entry,
                                  SolAccountInfo *funding_account, const SolPubkey *stake_account_key,
                                  const SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
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

    // If there is commission to take, do so
    if (commission_lamports > 0) {
        // The commission to charge is at not at least the minimum stake account size, so to charge commission:
        // - Split commission_lamports off of stake_account into bridge_stake_account
        // - Merge bridge_stake_account into master_stake_account
        return move_stake_signed(stake_account_key, &(Constants.master_stake_pubkey), commission_lamports,
                                 funding_account, transaction_accounts, transaction_accounts_len);
    }
    else {
        return 0;
    }
}
