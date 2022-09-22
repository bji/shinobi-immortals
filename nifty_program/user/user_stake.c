#pragma once

#include "util/util_stake.c"


static uint64_t user_stake(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,  block_account,                 ReadOnly,  NotSigner,   KnownAccount_NotKnown);
        DECLARE_ACCOUNT(1,  entry_account,                 ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,  token_owner_account,           ReadOnly,   Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(3,  token_account,                 ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(4,  stake_account,                 ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(5,  withdraw_authority_account,    ReadOnly,   Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(6,  shinobi_systems_vote_account,  ReadOnly,   NotSigner,  KnownAccount_ShinobiSystemsVote);
        DECLARE_ACCOUNT(7,  authority_account,             ReadOnly,   NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(8,  clock_sysvar_account,          ReadOnly,   NotSigner,  KnownAccount_ClockSysvar);
        DECLARE_ACCOUNT(9,  stake_program_account,         ReadOnly,   NotSigner,  KnownAccount_StakeProgram);
        DECLARE_ACCOUNT(10, stake_config_account,          ReadOnly,   NotSigner,  KnownAccount_StakeConfig);
        DECLARE_ACCOUNT(11, stake_history_sysvar_account,  ReadOnly,   NotSigner,  KnownAccount_StakeHistorySysvar);
    }
    DECLARE_ACCOUNTS_NUMBER(12);

    // This is the block data
    const Block *block = get_validated_block(block_account);
    if (!block) {
        return Error_InvalidAccount_First;
    }

    // This is the entry data
    Entry *entry = get_validated_entry_of_block(entry_account, block_account->key);
    if (!entry) {
        return Error_InvalidAccount_First + 1;
    }

    // Get the clock sysvar, needed below
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }

    // Check to make sure that the entry is in an Owned state, which is the only state from which a stake operation is
    // valid.
    if (get_entry_state(0, entry, &clock) != EntryState_Owned) {
        return Error_NotStakeable;
    }

    // Deserialize the stake account into a Stake instance
    Stake stake;
    if (!decode_stake_account(stake_account, &stake)) {
        return Error_InvalidAccount_First + 3;
    }

    // - Must be in Initialized or Stake state
    switch (stake.state) {
    case StakeState_Initialized:
    case StakeState_Stake:
        break;
    default:
        return Error_InvalidAccount_First + 4;
    }

    // - Must have a withdraw authority equal to the provided withdraw authority
    if (!SolPubkey_same(&(stake.meta.authorize.withdrawer), withdraw_authority_account->key)) {
        return Error_InvalidAccount_First + 5;
    }

    // - Must not be locked.  Don't bother checking custodian, that feature just isn't supported here
    if ((stake.meta.lockup.unix_timestamp > clock.unix_timestamp) || (stake.meta.lockup.epoch > clock.epoch)) {
        return Error_InvalidAccount_First + 4;
    }

    // Check to make sure that the entry token is owned by the token owner account
    if (!is_token_owner(token_account, token_owner_account->key, &(entry->mint_pubkey), 1)) {
        return Error_InvalidAccount_First + 4;
    }

    // Use stake account program to set all authorities to the nifty authority
    if (set_stake_authorities(stake_account->key, withdraw_authority_account->key,
                              &(Constants.nifty_authority_pubkey), params->ka, params->ka_num)) {
        return Error_SetStakeAuthoritiesFailed;
    }

    // If the stake account is in an initialized state, then it's not delegated, so delegate it to Shinobi Systems.
    // The amount of SOL that it will have as delegated after this delegation
    if (stake.state == StakeState_Initialized) {
        if (delegate_stake_signed(stake_account->key, &(Constants.shinobi_systems_vote_pubkey),
                                  params->ka, params->ka_num)) {
            return Error_FailedToDelegate;
        }

        // Re-decode the stake account, to get the new delegation information
        if (!decode_stake_account(stake_account, &stake)) {
            return Error_InvalidAccount_First + 4;
        }
    }
    // Else the stake account is delegated (because the only other stake state possible is StakeState_Stake according
    // to the switch done already above), and if it's not delegated to Shinobi Systems, deactivate it, so that in the
    // next epoch it can be re-delegated to Shinobi Systems via the redelegate crank.
    else if (!is_shinobi_systems_vote_account(&(stake.stake.delegation.voter_pubkey))) {
        if (deactivate_stake_signed(stake_account->key, params->ka, params->ka_num)) {
            return Error_FailedToDeactivate;
        }

        // The delegated stake will be updated when anyone_take_commission_or_delegate next happens and successfully
        // delegates to Shinobi Systems
    }

    // Record the stake account address
    entry->owned.stake_account = *(stake_account->key);

    // Record initial lamports, to be used to calculate APY if needed
    entry->owned.stake_initial_lamports = stake.stake.delegation.stake;

    // Record stake epoch, to be used to calculate APY if needed
    entry->owned.stake_epoch = clock.epoch;

    // Record current lamports in the stake account to be used for ki harvesting purposes
    entry->owned.last_ki_harvest_stake_account_lamports = stake.stake.delegation.stake;

    // Record current lamports in the stake account to be used for commission purposes
    entry->owned.last_commission_charge_stake_account_lamports = stake.stake.delegation.stake;

    // Update the entry's commission to that of the block
    entry->commission = block->commission;

    return 0;
}
