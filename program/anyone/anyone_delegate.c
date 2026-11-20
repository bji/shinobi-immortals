#pragma once

#include "util/util_ki.c"
#include "util/util_stake.c"


// Delegates the stake account of a staked entry to Shinobi Systems, if it is not actively delegated.
//
// Stake accepts a stake account that is not delegated, or that is delegated to some other validator; in the latter
// case it deactivates the stake account.  Such a stake account earns no Ki until it is delegated to Shinobi Systems,
// which this instruction does.  A stake account delegated to Shinobi Systems can also end up deactivated without this
// program doing it: anyone can deactivate it with the Stake program's DeactivateDelinquent instruction if the Shinobi
// Systems validator has been delinquent for long enough.  This instruction re-delegates such a stake account too.  It
// is permissionless because it can do only one thing, which is what the program would always want done: the vote
// account is fixed, and the instruction does nothing if the stake account is already actively delegated.
//
// Re-delegating sets the stake account's delegated stake to its entire balance less its rent exempt reserve, which
// can include lamports that were deposited into it while it was inactive; those are not staking rewards.  So the
// entry's Ki baseline is moved up by exactly as much as the delegated stake grows, which keeps the deposits out of
// Ki, while keeping any Shinobi Systems rewards that were earned before the deactivation and not yet harvested owed
// to the entry's owner.  This instruction cannot pay that Ki itself, since it is permissionless and has no Ki
// destination; it is paid by the owner's next Harvest, LevelUp or Destake.

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
    // deactivated stake account is either one that user_stake deactivated because it was delegated to a different
    // validator, or one delegated to Shinobi Systems that was deactivated by the Stake program's DeactivateDelinquent
    // instruction.
    if ((stake.state != StakeState_Initialized) && (stake.state != StakeState_Stake)) {
        return Error_InvalidAccount_First + 2;
    }

    // Before re-delegating, count the epochs staked under the stake account's current delegation, which re-delegating
    // replaces.  This counts the epochs in which a delegation to Shinobi Systems was effective before it was
    // deactivated (by the Stake program's DeactivateDelinquent instruction), up to the current epoch.  The new
    // delegation's activation epoch is the current epoch, so it is counted from the next epoch on.
    count_epochs_staked(&stake, entry, clock.epoch);

    // Before re-delegating, determine the Shinobi Systems staking rewards that the stake account has earned and that
    // have not been harvested yet: the amount by which its delegated stake exceeds the entry's Ki baseline.  This is
    // only the case for a stake account that was delegated to Shinobi Systems (see is_delegated_to_shinobi_systems);
    // for an Initialized stake account, or one that was delegated to another validator, nothing is owed, and any
    // increase of its delegated stake over the baseline is that other validator's rewards, which never earn Ki.
    bool was_delegated_to_shinobi_systems = is_delegated_to_shinobi_systems(&stake);
    uint64_t previous_delegated_stake = 0;
    uint64_t unharvested_lamports = 0;
    if (was_delegated_to_shinobi_systems) {
        previous_delegated_stake = stake.stake.delegation.stake;
        if (previous_delegated_stake > entry->owned.last_ki_harvest_stake_account_lamports) {
            unharvested_lamports = previous_delegated_stake - entry->owned.last_ki_harvest_stake_account_lamports;
        }
    }

    // Delegate it to Shinobi Systems.  If it is still cooling down, the stake program will refuse the delegation and
    // this instruction will fail; it will succeed if retried after the cooldown completes.
    delegate_stake_signed(stake_account->key, &(Constants.shinobi_systems_vote_pubkey), params->ka, params->ka_num);

    // Re-decode the stake account, to get the new delegation information
    if (!decode_stake_account(stake_account, &stake)) {
        return Error_InvalidAccount_First + 2;
    }

    // This is the newly delegated stake
    uint64_t delegated_stake = stake.stake.delegation.stake;

    // Set the Ki baseline so that only Shinobi Systems staking rewards ever earn Ki:
    //
    // - If the stake account was delegated to Shinobi Systems, then the baseline moves up by exactly as much as the
    //   delegated stake grew in being re-delegated, which is lamports deposited into the stake account while it was
    //   inactive.  Those are not counted as rewards, while the unharvested rewards stay owed: the baseline is the new
    //   delegated stake less the unharvested rewards.  The delegated stake cannot shrink in being re-delegated, since
    //   nothing can withdraw from the stake account while this program holds its withdraw authority; but if it ever
    //   did, nothing is carried over, so that the baseline can never be set below what is certain to be rewards.
    //
    // - Otherwise the baseline is reset to the newly delegated stake, so that neither rewards earned while delegated
    //   elsewhere nor any lamports deposited into the stake account while it was inactive are counted as Shinobi
    //   Systems staking rewards.
    if (was_delegated_to_shinobi_systems && (delegated_stake >= previous_delegated_stake)) {
        // unharvested_lamports <= previous_delegated_stake <= delegated_stake, so this cannot underflow
        entry->owned.last_ki_harvest_stake_account_lamports = delegated_stake - unharvested_lamports;
    }
    else {
        entry->owned.last_ki_harvest_stake_account_lamports = delegated_stake;
    }

    return 0;
}
