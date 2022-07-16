#pragma once


typedef struct
{
    uint8_t instruction_code; // instruction code of SplitMasterStake

    uint64_t lamports; // Number of lamports of stake to split off
    
} SplitMasterStakeData;
    

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

    // Check input data size
    if (params->data_len != sizeof(SplitMasterStakeData)) {
        return Error_InvalidDataSize;
    }

    // Input data is safe to use now
    SplitMasterStakeData *data = (SplitMasterStakeData *) params->data;

    // Decode the master stake account
    Stake stake;
    if (!decode_stake_account(master_stake_account, &stake)) {
        return Error_InvalidAccount_First + 2;
    }

    // Check to make sure that splitting the requested lamports off of the master stake account would not reduce its
    // lamports to less than the minimum allowed for the master stake account
    if (stake.stake.delegation.stake < (data->lamports + MASTER_STAKE_ACCOUNT_MIN_LAMPORTS)) {
        return Error_InvalidData_First;
    }

    return split_master_stake_signed(target_stake_account->key, data->lamports, admin_account->key,
                                     params->ka, params->ka_num);
}
