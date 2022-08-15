#pragma once

typedef struct
{
    // This is the instruction code for SetBlockCommission
    uint8_t instruction_code;

    // The new commission for this block.  If it's higher than the previous block commission, then it must be no more
    // than 2% higher.  Also cannot be changed more than once per epoch.  And finally, only takes effect *after* the
    // next commission charge for any already-staked stake accounts.
    uint16_t commission;
    
} SetBlockCommissionData;


static uint64_t admin_set_block_commission(SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,  config_account,                ReadOnly,  NotSigner,   KnownAccount_ProgramConfig);
        DECLARE_ACCOUNT(1,  admin_account,                 ReadOnly,  Signer,      KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,  block_account,                 ReadOnly,  NotSigner,   KnownAccount_NotKnown);
    }
    DECLARE_ACCOUNTS_NUMBER(3);

    // Ensure the the transaction has been authenticated by the admin
    if (!is_admin_account(config_account, admin_account->key)) {
        return Error_PermissionDenied;
    }
    
    // Ensure that the data is the correct size
    if (params->data_len != sizeof(SetBlockCommissionData)) {
        return Error_InvalidDataSize;
    }

    // Now the data can be used
    SetBlockCommissionData *data = (SetBlockCommissionData *) params->data;

    // This is the block data
    Block *block = get_validated_block(block_account);
    if (!block) {
        return Error_InvalidAccount_First;
    }

    // If the block is not complete, then can't make this change
    if (!is_block_complete(block)) {
        return Error_BlockNotComplete;
    }

    // Get the clock sysvar, needed below
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }

    // Ensure that the block's last commission update is older than the current epoch
    if (block->last_commission_change_epoch == clock.epoch) {
        return Error_CommissionAlreadySetThisEpoch;
    }

    // Ensure that the new commission is no more than 2% higher than the previous commission
    if (data->commission > (block->commission + 1310)) {
        return Error_CommissionTooHigh;
    }

    // Commission change approved, so set it and record the epoch in which the change was mode so that it
    // can't be changed again this epoch
    block->commission = data->commission;
    block->last_commission_change_epoch = clock.epoch;
    
    return 0;
}
