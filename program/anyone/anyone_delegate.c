#pragma once


// Delegates the stake account of a staked entry to Shinobi Systems, if it is not actively delegated.
//
// Stake accepts a stake account that is not delegated, or that is delegated to some other validator; in the latter
// case it deactivates the stake account.  Such a stake account earns no Ki until it is delegated to Shinobi Systems,
// which this instruction does.  It is permissionless because it can do only one thing, which is what the program
// would always want done: the vote account is fixed, and the instruction does nothing if the stake account is
// already actively delegated.

static uint64_t anyone_delegate(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,  block_account,                 ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(1,  entry_account,                 ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,  stake_account,                 ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(3,  authority_account,             ReadOnly,   NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(4,  clock_sysvar_account,          ReadOnly,   NotSigner,  KnownAccount_ClockSysvar);
        DECLARE_ACCOUNT(5,  stake_program_account,         ReadOnly,   NotSigner,  KnownAccount_StakeProgram);
        DECLARE_ACCOUNT(6,  stake_history_sysvar_account,  ReadOnly,   NotSigner,  KnownAccount_StakeHistorySysvar);
        DECLARE_ACCOUNT(7,  shinobi_systems_vote_account,  ReadOnly,   NotSigner,  KnownAccount_ShinobiSystemsVote);
        DECLARE_ACCOUNT(8,  stake_config_account,          ReadOnly,   NotSigner,  KnownAccount_StakeConfig);
    }
    DECLARE_ACCOUNTS_NUMBER(9);

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

    // If it's in a staked state and not deactivated, then it is delegated to Shinobi Systems: user_stake only leaves a
    // delegated stake account active if it is delegated to Shinobi Systems, and deactivates all others.  There is
    // nothing to do, and this is reported as an error so that the caller knows that nothing was done.
    if ((stake.state == StakeState_Stake) && (stake.stake.delegation.deactivation_epoch == UINT64_MAX)) {
        return Error_AlreadyDelegated;
    }

    // The only other states that are acceptable are Initialized, and delegated stake that has been deactivated.  A
    // deactivated stake account can only be one that user_stake deactivated because it was delegated to a different
    // validator.
    if ((stake.state != StakeState_Initialized) && (stake.state != StakeState_Stake)) {
        return Error_InvalidAccount_First + 2;
    }

    // Delegate it to Shinobi Systems.  If it is still cooling down, the stake program will refuse the delegation and
    // this instruction will fail; it will succeed if retried after the cooldown completes.
    delegate_stake_signed(stake_account->key, &(Constants.shinobi_systems_vote_pubkey), params->ka, params->ka_num);

    // Re-decode the stake account, to get the new delegation information
    if (!decode_stake_account(stake_account, &stake)) {
        return Error_InvalidAccount_First + 2;
    }

    // The Ki baseline is reset to the newly delegated stake, so that neither rewards earned while delegated elsewhere
    // nor any lamports deposited into the stake account while it was inactive are counted as Shinobi Systems staking
    // rewards.
    entry->owned.last_ki_harvest_stake_account_lamports = stake.stake.delegation.stake;

    return 0;
}
