#pragma once

#include "util/util_stake.c"


// funding_account is only used to provide transient quantities of SOL for a temporary stake account
static uint64_t charge_commission(Stake *stake, Block *block, Entry *entry, SolPubkey *funding_account_key,
                                  SolAccountInfo *bridge_stake_account, SolPubkey *stake_account_key,
                                  SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    // Compute commission to charge.  It is the commission as set in the block, times the difference between
    // the current lamports in the stake account minus the lamports that were in the stake account the last
    // time commission was charged.
    uint64_t commission_lamports =
        (((stake->stake.delegation.stake - entry->owned.last_commission_charge_stake_account_lamports) *
          entry->commission) / 0xFFFFull);

    // Update the entry's last_commission_charge_stake_account_lamports to the new value.  This is done
    // unconditionally in case the commission has changed -- even if the old commission was zero and thus no
    // commission is charged right now, the state needs to be updated so that these earnings are not charged
    // commission again.
    entry->owned.last_commission_charge_stake_account_lamports = stake->stake.delegation.stake;

    // If there is commission to take, do so
    if (commission_lamports == 0) {
        return 0;
    }

    // Update the entry's commission with the current value from the block, so that the next commission collection
    // will use it.  In this way, when a block's commission is changed, it doesn't affect any given entry until that
    // entry has had at least one commission collection.
    entry->commission = block->commission;

    // Compute the bridge address
    uint8_t prefix = PDA_Account_Seed_Prefix_Bridge;
    
    uint8_t bump_seed;
    
    SolSignerSeed seeds[] = { { &prefix, sizeof(prefix) },
                              { (uint8_t *) &(entry->mint_pubkey), sizeof(entry->mint_pubkey) },
                              { &bump_seed, sizeof(bump_seed) } };

    SolPubkey pubkey;
    uint64_t ret = sol_try_find_program_address(seeds, ARRAY_LEN(seeds) - 1, &(Constants.nifty_program_pubkey),
                                                &pubkey, &bump_seed);
    if (ret) {
        return ret;
    }
    
    // Verify that the bridge address is as expected
    if (!SolPubkey_same(&pubkey, bridge_stake_account->key)) {
        return Error_CreateAccountFailed;
    }

    // Compute the minimum lamports which can be split off into a stake account
    uint64_t minimum_stake_lamports;
    ret = get_minimum_stake_delegation(&minimum_stake_lamports);
    if (ret) {
        return ret;
    }
    
    // If the commission to charge is less than the minimum stake in lamports, then it is necessary to use a more
    // complex mechanism to avoid ever having a temporary stake account of less than the minimum:
    // - Split minimum_stake_lamports off of master_stake_account into bridge_stake_account
    // - Merge bridge_stake_account into stake_account
    // - Set the commission_lamports to (minimum_stake_lamports + commission_lamports) and continue with a normal
    //   commission charge (which will return the bridge lamports as well)
    if (commission_lamports < minimum_stake_lamports) {
        if (move_stake_signed(&(Constants.master_stake_pubkey), bridge_stake_account, seeds, ARRAY_LEN(seeds),
                              stake_account_key, minimum_stake_lamports, funding_account_key, transaction_accounts,
                              transaction_accounts_len)) {
            return Error_FailedToMoveStakeOut;
        }
        commission_lamports += minimum_stake_lamports;
    }

    // The commission to charge is at not at least the minimum stake account size, so to charge commission:
    // - Split commission_lamports off of stake_account into bridge_stake_account
    // - Merge bridge_stake_account into master_stake_account
    if (move_stake_signed(stake_account_key, bridge_stake_account, seeds, ARRAY_LEN(seeds),
                          &(Constants.master_stake_pubkey), commission_lamports, funding_account_key,
                          transaction_accounts, transaction_accounts_len)) {
        return Error_FailedToMoveStake;
    }

    return 0;
}
