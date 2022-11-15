#pragma once


static uint64_t anyone_take_commission_or_delegate(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,  funding_account,               ReadWrite,  Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(1,  block_account,                 ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,  entry_account,                 ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(3,  stake_account,                 ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(4,  master_stake_account,          ReadWrite,  NotSigner,  KnownAccount_MasterStake);
        DECLARE_ACCOUNT(5,  bridge_stake_account,          ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(6,  authority_account,             ReadOnly,   NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(7,  clock_sysvar_account,          ReadOnly,   NotSigner,  KnownAccount_ClockSysvar);
        DECLARE_ACCOUNT(8,  system_program_account,        ReadOnly,   NotSigner,  KnownAccount_SystemProgram);
        DECLARE_ACCOUNT(9,  stake_program_account,         ReadOnly,   NotSigner,  KnownAccount_StakeProgram);
        DECLARE_ACCOUNT(10, stake_history_sysvar_account,  ReadOnly,   NotSigner,  KnownAccount_StakeHistorySysvar);
    }
    DECLARE_ACCOUNTS_NUMBER(11);

    // Get validated block and entry, which checks all validity of those accounts
    const Block *block = get_validated_block(block_account);
    if (!block) {
        return Error_InvalidAccount_First + 1;
    }

    // Ensure that the block is complete; cannot stake in a block that is not complete yet
    if (!is_block_complete(block)) {
        return Error_BlockNotComplete;
    }

    // This is the entry data
    Entry *entry = get_validated_entry_of_block(entry_account, block_account->key);
    if (!entry) {
        return Error_InvalidAccount_First + 2;
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
        return Error_InvalidAccount_First + 3;
    }

    // Decode the stake account
    Stake stake;
    if (!decode_stake_account(stake_account, &stake)) {
        return Error_InvalidAccount_First + 3;
    }

    // If the stake account is in an initialized state, then it's not delegated, so delegate it to Shinobi Systems
    if (stake.state == StakeState_Initialized) {
        uint64_t ret = delegate_stake_signed(stake_account->key, &(Constants.shinobi_systems_vote_pubkey),
                                             params->ka, params->ka_num);
        if (ret) {
            return ret;
        }

        // Re-decode the stake account, to get the new delegation information
        if (!decode_stake_account(stake_account, &stake)) {
            return Error_InvalidAccount_First + 3;
        }

        // Record current lamports in the stake account to be used for ki harvesting purposes
        entry->owned.last_ki_harvest_stake_account_lamports = stake.stake.delegation.stake;

        // Record current lamports in the stake account to be used for commission purposes
        entry->owned.last_commission_charge_stake_account_lamports = stake.stake.delegation.stake;

        return 0;
    }
    // Else, it's initialized, so try charging commission
    else {
        return charge_commission(&stake, block, entry, funding_account->key, bridge_stake_account, stake_account->key,
                                 params->ka, params->ka_num);
    }
}
