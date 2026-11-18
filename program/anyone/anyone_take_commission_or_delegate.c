#pragma once


static uint64_t anyone_take_commission_or_delegate(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,  block_account,                 ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(1,  entry_account,                 ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,  stake_account,                 ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(3,  master_stake_account,          ReadWrite,  NotSigner,  KnownAccount_MasterStake);
        DECLARE_ACCOUNT(4,  authority_account,             ReadOnly,   NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(5,  clock_sysvar_account,          ReadOnly,   NotSigner,  KnownAccount_ClockSysvar);
        DECLARE_ACCOUNT(6,  system_program_account,        ReadOnly,   NotSigner,  KnownAccount_SystemProgram);
        DECLARE_ACCOUNT(7,  stake_program_account,         ReadOnly,   NotSigner,  KnownAccount_StakeProgram);
        DECLARE_ACCOUNT(8,  stake_history_sysvar_account,  ReadOnly,   NotSigner,  KnownAccount_StakeHistorySysvar);
        DECLARE_ACCOUNT(9,  shinobi_systems_vote_account,  ReadOnly,   NotSigner,  KnownAccount_ShinobiSystemsVote);
        DECLARE_ACCOUNT(10, stake_config_account,          ReadOnly,   NotSigner,  KnownAccount_StakeConfig);
    }
    DECLARE_ACCOUNTS_NUMBER(11);

    // This instruction takes no data beyond the instruction code byte, so ensure that no data was supplied
    if (params->data_len != 1) {
        return Error_InvalidDataSize;
    }

    // Get validated block and entry, which checks all validity of those accounts
    const Block *block = get_validated_block(block_account);
    if (!block) {
        return Error_InvalidAccount_First + 0;
    }

    // Ensure that the block is complete; cannot stake in a block that is not complete yet
    if (!is_block_complete(block)) {
        return Error_BlockNotComplete;
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

    // Check to make sure that the entry is staked
    if (get_entry_state(block, entry, &clock) != EntryState_OwnedAndStaked) {
        return Error_NotStaked;
    }

    // Check to make sure that the stake account passed in is actually staked in the entry
    if (!SolPubkey_same(&(entry->owned.stake_account), stake_account->key)) {
        return Error_InvalidAccount_First + 2;
    }

    // Decode the stake account
    Stake stake;
    if (!decode_stake_account(stake_account, &stake)) {
        return Error_InvalidAccount_First + 2;
    }

    // If the stake account is in an initialized state, or is delegated stake that has been deactivated, then delegate
    // it to Shinobi Systems.  A deactivated stake account can only be one that user_stake deactivated because it was
    // delegated to a different validator.  If it is still cooling down, the stake program will refuse the delegation
    // and this instruction will fail; it will succeed if retried after the cooldown completes.
    //
    // The Ki and commission baselines are reset to the newly delegated stake, so that neither rewards earned while
    // delegated elsewhere nor any lamports deposited into the stake account while it was inactive are counted as
    // Shinobi Systems staking rewards.
    if ((stake.state == StakeState_Initialized) ||
        ((stake.state == StakeState_Stake) &&
         (stake.stake.delegation.deactivation_epoch != UINT64_MAX))) {
        delegate_stake_signed(stake_account->key, &(Constants.shinobi_systems_vote_pubkey),
                              params->ka, params->ka_num);

        // Re-decode the stake account, to get the new delegation information
        if (!decode_stake_account(stake_account, &stake)) {
            return Error_InvalidAccount_First + 2;
        }

        // Record current lamports in the stake account to be used for ki harvesting purposes
        entry->owned.last_ki_harvest_stake_account_lamports = stake.stake.delegation.stake;

        // Record current lamports in the stake account to be used for commission purposes
        entry->owned.last_commission_charge_stake_account_lamports = stake.stake.delegation.stake;

        // No commission is owed on the newly delegated stake, so start counting epochs without a commission charge
        // from now (see DESTAKE_COMMISSION_WAIVER_EPOCHS)
        entry->last_commission_charge_epoch = clock.epoch;

        return 0;
    }
    // Else, if it's in a staked state, then it is delegated to Shinobi Systems and not deactivated: user_stake only
    // leaves a delegated stake account active if it is delegated to Shinobi Systems, and deactivates all others, which
    // are handled above.  So try charging commission.
    else if (stake.state == StakeState_Stake) {
        charge_commission(&stake, block, entry, stake_account->key, clock.epoch, params->ka, params->ka_num);

        return 0;
    }
    // Else, this is a request using a stake account in an unacceptable state, so return an error
    else {
        return Error_InvalidAccount_First + 2;
    }
}
