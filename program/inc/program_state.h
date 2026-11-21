#pragma once

#include "data_type.h"
#include "types.h"


// Shared program state: administrative authority, and the reward rate that calibrates level credit curves.
typedef struct
{
    // The data type of the data -- DataType_ProgramState
    DataType data_type;

    // The pubkey of the admin user, which is the only user who has rights to execute the admin functions.  This
    // account must sign any transaction requiring admin privileges.
    SolPubkey admin_pubkey;

    // The annual reward rate of a delegator to Shinobi Systems, as a binary fraction: the rate is reward_rate / 2^32
    // (so 6.5% is about 279,172,874).  Initialize sets it to the superuser's estimate, and MeasureRewardRate then
    // updates it from measurements of the reference stake account.  It is always kept between REWARD_RATE_MIN and
    // REWARD_RATE_MAX (see util_calculations.c).  LevelUp uses it, rounded to whole basis points, to calibrate every
    // entry's level credit curve.
    uint32_t reward_rate;

    // The level credit window (Clock.unix_timestamp / LEVEL_CREDIT_WINDOW_SECONDS) in which reward_rate was last
    // set: by a measurement, or by Initialize.  MeasureRewardRate measures at most once per window.
    uint64_t reward_rate_window;

    // The snapshot of the reference stake account that the next measurement is made from: its delegated stake, as of
    // the start of an epoch, after that epoch's staking rewards had all been paid; that epoch; and the epoch's start
    // time (Clock.epoch_start_timestamp).  The next measurement compares the delegated stake then with this, over the
    // time from the start of this epoch to the start of the epoch then.  A snapshot_stake of zero means that there is
    // no snapshot (as before the first, or after the reference stake account stopped earning), and the next
    // MeasureRewardRate takes one rather than measuring.
    uint64_t snapshot_stake;

    uint64_t snapshot_epoch;

    timestamp_t snapshot_epoch_start_timestamp;

} ProgramState;
