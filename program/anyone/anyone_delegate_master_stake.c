#pragma once

// Delegates the master stake account to Shinobi Systems, if it is not actively delegated.
//
// Nothing in this program ever deactivates the master stake account.  But the stake program's DeactivateDelinquent
// instruction allows anyone to deactivate any stake account that is delegated to a validator which has not voted for
// five epochs, and that would include the master stake account if Shinobi Systems were ever to stop voting for that
// long.  This instruction is the way back: once the master stake account has finished cooling down, anyone may use
// this instruction to delegate it to Shinobi Systems again.
//
// This instruction is permissionless because it can do only one thing, which is what the program would always want
// done: the vote account is fixed, and the instruction does nothing if the master stake account is already actively
// delegated.
//
// Notes:
//
// - While the master stake account is still cooling down, the stake program refuses the delegation and this
//   instruction fails.  The exception is the epoch in which it was deactivated, in which the stake program allows the
//   deactivation to be rescinded.
// - After the master stake account has cooled down, a commission charge (which moves stake into it) makes it actively
//   delegated again all by itself, but with only the moved stake delegated; the rest of its balance is left as lamports
//   that are not staked.  This instruction then no longer applies, and those lamports can be recovered by the admin
//   with the WithdrawExcess instruction instead.

static uint64_t anyone_delegate_master_stake(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,  master_stake_account,          ReadWrite,  NotSigner,  KnownAccount_MasterStake);
        DECLARE_ACCOUNT(1,  authority_account,             ReadOnly,   NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(2,  clock_sysvar_account,          ReadOnly,   NotSigner,  KnownAccount_ClockSysvar);
        DECLARE_ACCOUNT(3,  stake_program_account,         ReadOnly,   NotSigner,  KnownAccount_StakeProgram);
        DECLARE_ACCOUNT(4,  stake_history_sysvar_account,  ReadOnly,   NotSigner,  KnownAccount_StakeHistorySysvar);
        DECLARE_ACCOUNT(5,  shinobi_systems_vote_account,  ReadOnly,   NotSigner,  KnownAccount_ShinobiSystemsVote);
        DECLARE_ACCOUNT(6,  stake_config_account,          ReadOnly,   NotSigner,  KnownAccount_StakeConfig);
    }
    DECLARE_ACCOUNTS_NUMBER(7);

    // This instruction takes no data beyond the instruction code byte, so ensure that no data was supplied
    if (params->data_len != 1) {
        return Error_InvalidDataSize;
    }

    // Decode the master stake account
    Stake stake;
    if (!decode_stake_account(master_stake_account, &stake)) {
        return Error_InvalidAccount_First + 0;
    }

    // The master stake account is actively delegated if it is in the Stake state and has not been deactivated.  In
    // that case there is nothing to do, and this is reported as an error so that the caller knows that nothing was
    // done.
    if ((stake.state == StakeState_Stake) && (stake.stake.delegation.deactivation_epoch == UINT64_MAX)) {
        return Error_MasterStakeAlreadyDelegated;
    }

    // The only other states that a stake account which this program initialized can be in are Initialized, and Stake
    // with a deactivation epoch.  Anything else is not a usable master stake account.
    if ((stake.state != StakeState_Initialized) && (stake.state != StakeState_Stake)) {
        return Error_InvalidAccount_First + 0;
    }

    // Delegate the master stake account to Shinobi Systems, signed by the program authority, which is the stake
    // authority of the master stake account.  The stake program delegates all of the lamports of the account beyond
    // its rent exempt reserve.  If the account is still cooling down, the stake program refuses, which aborts the
    // transaction.
    delegate_stake_signed(master_stake_account->key, &(Constants.shinobi_systems_vote_pubkey), params->ka,
                          params->ka_num);

    return 0;
}
