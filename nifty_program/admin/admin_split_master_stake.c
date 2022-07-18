#pragma once


static uint64_t admin_split_master_stake(SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,  config_account,                ReadOnly,   NotSigner,  KnownAccount_ProgramConfig);
        DECLARE_ACCOUNT(1,  admin_account,                 ReadOnly,   Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,  master_stake_account,          ReadWrite,  NotSigner,  KnownAccount_MasterStake);
        DECLARE_ACCOUNT(3,  target_stake_account,          ReadWrite,  Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(4,  authority_account,             ReadOnly,   NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(5,  clock_sysvar,                  ReadOnly,   NotSigner,  KnownAccount_ClockSysvar);
        DECLARE_ACCOUNT(6,  system_program_account,        ReadOnly,   NotSigner,  KnownAccount_SystemProgram);
        DECLARE_ACCOUNT(7,  stake_program_account,         ReadOnly,   NotSigner,  KnownAccount_StakeProgram);
    }
    DECLARE_ACCOUNTS_NUMBER(8);

    // Ensure that this transaction is admin authenticated
    if (!is_admin_authenticated(config_account, admin_account)) {
        return Error_PermissionDenied;
    }

    // Decode the master stake account
    Stake stake;
    if (!decode_stake_account(master_stake_account, &stake)) {
        return Error_InvalidAccount_First + 2;
    }

    // Determine the maximum number of lamports that could be split off from the master stake account and still
    // leave it at its minimum delegation.  Split that number of lamports off.
    if (stake.stake.delegation.stake <= MASTER_STAKE_ACCOUNT_MIN_LAMPORTS) {
        // No lamports to split off
        return 0;
    }

    return split_master_stake_signed(target_stake_account->key,
                                     stake.stake.delegation.stake - MASTER_STAKE_ACCOUNT_MIN_LAMPORTS,
                                     admin_account->key, params->ka, params->ka_num);
}
