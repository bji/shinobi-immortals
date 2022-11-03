#pragma once

typedef struct
{
    // This is the instruction code for SplitMasterStake
    uint8_t instruction_code;

    // Number of lamports to split off, or 0 to split the maximum possible.
    uint64_t lamports;

} SplitMasterStakeData;


// The Pre-Merge account, if it is non-zero, is a stake account that will be merged into the master stake account
// prior to splitting stake from the master account.  This allows smaller-than-minimum-stake-account-size stake
// to be split off of the master stake account: the merge will presumably push the master stake account to a size
// large enough that a split of everything remaining would produce a split-stake where neither side is smaller
// than the minimum.

// i.e. if the minimum is 100 SOL (god forbid), and the master stake account has 120 SOL, then the only way to
// split off of 20 (which can't be done directly as the split account would be 20 SOL, lower than the minimum of
// 100) would be to first merge in an 80 SOL stake account, resulting in a 200 SOL master account, then split
// 100 SOL off of it.

// This means that the merge account has to be delegated to Shinobi Systems and have earned stake rewards, and
// it also must have the program authority as its stake and withdraw authorities.

static uint64_t admin_split_master_stake(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,  config_account,                ReadOnly,   NotSigner,  KnownAccount_ProgramConfig);
        DECLARE_ACCOUNT(1,  admin_account,                 ReadOnly,   Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,  master_stake_account,          ReadWrite,  NotSigner,  KnownAccount_MasterStake);
        DECLARE_ACCOUNT(3,  split_into_account,            ReadWrite,  Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(4,  pre_merge_account,             ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(5,  authority_account,             ReadOnly,   NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(6,  clock_sysvar,                  ReadOnly,   NotSigner,  KnownAccount_ClockSysvar);
        DECLARE_ACCOUNT(7,  system_program_account,        ReadOnly,   NotSigner,  KnownAccount_SystemProgram);
        DECLARE_ACCOUNT(8,  stake_program_account,         ReadOnly,   NotSigner,  KnownAccount_StakeProgram);
        DECLARE_ACCOUNT(9,  stake_history_sysvar_account,  ReadOnly,   NotSigner,  KnownAccount_StakeHistorySysvar);
    }
    DECLARE_ACCOUNTS_NUMBER(10);

    // Ensure that this transaction is admin authenticated
    if (!is_admin_account(config_account, admin_account->key)) {
        return Error_PermissionDenied;
    }

    // If the pre_merge_account is not all zeroes, then it must be ReadWrite
    if (is_empty_pubkey(pre_merge_account->key)) {
        pre_merge_account = 0;
    }
    else if (!pre_merge_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 3;
    }

    // Get the instruction data
    if (params->data_len < sizeof(SplitMasterStakeData)) {
        return Error_InvalidDataSize;
    }

    const SplitMasterStakeData *data = (SplitMasterStakeData *) params->data;

    // Decode the master stake account
    Stake stake;
    if (!decode_stake_account(master_stake_account, &stake)) {
        return Error_InvalidAccount_First + 2;
    }

    // Determine the maximum number of lamports that could be split off from the master stake account and still
    // leave it at its minimum delegation.
    uint64_t to_split = stake.stake.delegation.stake - MASTER_STAKE_ACCOUNT_MIN_LAMPORTS;

    // If a number of lamports to split was specified ...
    if (data->lamports > 0) {
        // The then if that amount is more that can be split, it is an error
        if (data->lamports > to_split) {
            return Error_InvalidData_First + 3;
        }
        // Otherwise, split that amount
        else {
            to_split = data->lamports;
        }
    }

    // If the pre_merge_account was given, then its stake would be split out after being merged too
    if (pre_merge_account) {
        // Decode the pre_merge stake account
        if (!decode_stake_account(pre_merge_account, &stake)) {
            return Error_InvalidAccount_First + 4;
        }
        to_split += stake.stake.delegation.stake;
    }

    // The split will be into the master_stake_split_account, which will be set with the withdraw authority of
    // the admin account
    return split_master_stake_signed(admin_account->key, master_stake_account, pre_merge_account, split_into_account,
                                     to_split, params->ka, params->ka_num);
}
