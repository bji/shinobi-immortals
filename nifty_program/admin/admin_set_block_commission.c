#pragma once


typedef struct
{
    // This is the instruction code for SetBlockCommission
    uint8_t instruction_code;

    // The new commission for this block.  If it's higher than the previous block commission, then it must be no more
    // than 2% higher.  Also cannot be changed more than once per epoch.  And finally, only takes effect *after* the
    // next commission charge for any already-staked stake accounts.
    uint16_t commission;
    
} TakeCommissionData;

static uint64_t admin_set_block_commission(SolParameters *params)
{
    return 0;
}
