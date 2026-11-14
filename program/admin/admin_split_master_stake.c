#pragma once

typedef struct
{
    // This is the instruction code for SplitMasterStake
    uint8_t instruction_code;

    // Number of lamports to split off, or 0 to split the maximum possible.
    uint64_t lamports;

} SplitMasterStakeData;

// The Pre-Merge account, if it is non-zero, is a stake account that will be merged into the master stake account
// prior to splitting stake from the master account.  It must have the admin account as its withdraw authority, and
// must be a valid stake account actively delegated to Shinobi Systems and thus mergeable into the master stake
// account.
//
// This allows smaller-than-minimum-stake-account-size stake
// to be split off of the master stake account: the merge will presumably push the master stake account to a size
// large enough that a split of everything remaining would produce a split-stake where neither side is smaller
// than the minimum.

// i.e. if the minimum is 100 SOL (god forbid), and the master stake account has 120 SOL, then the only way to
// split off of 20 (which can't be done directly as the split account would be 20 SOL, lower than the minimum of
// 100) would be to first merge in a 100 SOL stake account, resulting in a 220 SOL master account, then split
// 120 SOL off of it.

static uint64_t admin_split_master_stake(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,  state_account,                 ReadOnly,   NotSigner,  KnownAccount_ProgramState);
        DECLARE_ACCOUNT(1,  admin_account,                 ReadWrite,  Signer,     KnownAccount_NotKnown);
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
    if (!is_admin_account(state_account, admin_account->key)) {
        return Error_PermissionDenied;
    }

    // Ensure that the instruction data is exactly the correct size, with no trailing data
    if (params->data_len != sizeof(SplitMasterStakeData)) {
        return Error_InvalidDataSize;
    }

    const SplitMasterStakeData *data = (SplitMasterStakeData *) params->data;

    // Decode the master stake account
    Stake stake;
    if (!decode_stake_account(master_stake_account, &stake)) {
        return Error_InvalidAccount_First + 2;
    }

    // The master stake's delegation should never be below its minimum, since nothing in this program reduces it below
    // that, but a future stake program change such as slashing could.  Refuse to split rather than letting the
    // subtraction below wrap to an enormous value, which would allow splitting off the master stake account's entire
    // balance; with the master stake account emptied, every commission charge (and therefore every Destake with
    // commission owed) would fail.  A delegation exactly at the mimimum is also rejected, since there's nothing left
    // to split.
    if (stake.stake.delegation.stake <= MASTER_STAKE_ACCOUNT_MIN_LAMPORTS) {
        return Error_InvalidData_First + 3;
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
    if (pre_merge_account && !is_empty_pubkey(pre_merge_account->key)) {
        // This must be a writable account to be a valid stake account
        if (!pre_merge_account->is_writable) {
            return Error_InvalidAccountPermissions_First + 4;
        }
        Stake pre_merge_account_decoded;
        // Decode the pre_merge stake account
        if (!decode_stake_account(pre_merge_account, &pre_merge_account_decoded)) {
            return Error_InvalidData_First + 4;
        }
        if (pre_merge_account_decoded.state != StakeState_Stake) {
            return Error_InvalidData_First + 4;
        }
        if (!is_admin_account(state_account, &(pre_merge_account_decoded.meta.authorize.withdrawer))) {
            return Error_InvalidData_First + 4;
        }
        // Any other errors with the stake account such as being inactive, etc, will simply cause the stake
        // merge to fail later

        // Increase the lamports to split back out of the master later to include those that will be merged in so that
        // the pre_merge_account's stake is returned to the admin
        to_split += pre_merge_account_decoded.stake.delegation.stake;
    }

    // The split will be into the master_stake_split_account, which will be set with the withdraw authority of
    // the admin account
    return split_master_stake_signed(admin_account->key, master_stake_account, pre_merge_account, split_into_account,
                                     to_split, params->ka, params->ka_num);
}
