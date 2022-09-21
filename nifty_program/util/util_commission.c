#pragma once

#include "util/util_stake.c"


// funding_account is only used to provide transient quantities of SOL for a temporary stake account
// If 0 is passed in for minimum_stake_lamports, the minimum will be fetched by calling the
// get_minimum_stake_delegation function.
static uint64_t charge_commission(Stake *stake, Block *block, Entry *entry, SolPubkey *funding_account_key,
                                  SolAccountInfo *bridge_stake_account, SolPubkey *stake_account_key,
                                  uint64_t minimum_stake_lamports,
                                  SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    // Compute commission to charge.  It is the commission as set in the block, times the difference between
    // the current lamports in the stake account minus the lamports that were in the stake account the last
    // time commission was charged.
    uint64_t commission_lamports =
        (((stake->stake.delegation.stake - entry->owned.last_commission_charge_stake_account_lamports) *
          entry->commission) / 0xFFFFull);

    // Update the entry's last_commission_charge_stake_account_lamports to the value it will hold after the commission
    // has been charged.
    entry->owned.last_commission_charge_stake_account_lamports = (stake->stake.delegation.stake - commission_lamports);

    // Update the entry's commission with the current value from the block, so that the next commission collection
    // will use it.  In this way, when a block's commission is changed, it doesn't affect any given entry until that
    // entry has had at least one commission collection.
    entry->commission = block->commission;

    // If there is commission to take, do so
    if (commission_lamports == 0) {
        return 0;
    }

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

    // Only if minimum_stake_lamports is passed in as 0 should get_minimum_stake_delegation be called.  This is
    // because that function relies on the currently buggy stake program's GetMinimumDelegation instruction
    // processing, and until that's fixed, the transaction data is going to have to supply the correct value.  If the
    // correct value is not supplied, then this transaction may fail because it will try to create a too-small bridge
    // stake account.  If the value supplied in the transaction is really large, then it will be harmless since the
    // lamports are always returned to the master stake account in any case.
    if (minimum_stake_lamports == 0) {
        ret = get_minimum_stake_delegation(&minimum_stake_lamports);
        if (ret) {
            return ret;
        }
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
