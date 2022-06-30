#pragma once

#include "inc/types.h"


// This is the type of the data stored in the clock sysvar
typedef struct
{
    uint64_t slot;

    timestamp_t epoch_start_timestamp;

    uint64_t epoch;

    uint64_t leader_schedule_epoch;

    timestamp_t unix_timestamp;
    
} Clock;


// Function missing from solana_sdk.h
extern uint64_t sol_get_clock_sysvar(void *ret);
