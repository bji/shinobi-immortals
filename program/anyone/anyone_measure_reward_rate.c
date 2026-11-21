#pragma once

#include "inc/clock.h"
#include "inc/program_state.h"
#include "util/util_calculations.c"
#include "util/util_stake.c"


// Measures the reward rate that a delegator to Shinobi Systems actually earns, and records it in the program state,
// where LevelUp uses it to calibrate every entry's level credit curve (see user_level_up.c).
//
// The level credit curve is calibrated by the annual reward rate, so that contributing the Ki that a 50 effective SOL
// stake account produces in a window levels an entry fully in about a year.  But real staking rewards change over
// time (inflation falls each year, and the reward rate also moves with the fraction of SOL that is staked), so a rate
// fixed when a block is created would drift further from the real one every year, and the block's entries would take
// longer and longer to level.  This instruction keeps the rate up to date by measuring it.
//
// The measurement is made with the reference stake account, a small stake account that Initialize created and
// delegated to Shinobi Systems, and that only the program controls.  Its delegated stake grows only by its staking
// rewards: lamports transferred to it are not delegated, and nothing ever withdraws from it, or adds to its delegation.
// So its growth is exactly what a delegator to Shinobi Systems earns, after commission and actual validator
// performance.  Its rewards stay delegated and compound, which does not matter, since the rate is measured relative to
// its delegated stake.
//
// Measurements are made between epoch boundaries, not between the times at which this instruction is run.  Rewards
// arrive in steps, once per epoch (they are paid at the start of the next epoch), so a rate computed as stake growth
// over the time between runs could be biased by choosing when to run: running just before an epoch's rewards arrive
// stretches the time without adding rewards.  So the program state records a snapshot: the delegated stake after an
// epoch's rewards had been paid, that epoch, and the epoch's start time.  A measurement takes the rewards of all of the
// whole epochs since that epoch (the growth of the delegated stake since the snapshot), over the time between the
// start of that epoch and the start of the current one, and then takes a new snapshot.  That is exact, and the moment
// within an epoch at which this instruction is run makes no difference.  Because rewards are paid over the first blocks
// of each epoch, this instruction measures, or takes a snapshot, only once all of the current epoch's rewards have
// been paid (see are_epoch_rewards_being_paid), so that no snapshot ever splits an epoch's rewards.
//
// At most one measurement is made per level credit window.  A window (5 days) spans more than one epoch (about 2
// days), so each measurement covers at least one whole epoch's rewards, and usually two or more.  If this instruction
// is not run in some windows, the next measurement simply covers the whole gap.  Each measurement is limited and
// smoothed before it is recorded (see compute_updated_reward_rate in util_calculations.c), so that no single unusual
// measurement can move the rate much.
//
// If the reference stake account is not earning rewards at Shinobi Systems (it is deactivated, cooling down, or warming
// up again after being delegated again), nothing is measured.  Its snapshot is discarded, and a new one is taken once
// it is earning again, so that no measurement ever spans a time in which it was not earning.  Nothing in this program
// ever deactivates it, but the stake program's DeactivateDelinquent instruction lets anyone deactivate a stake account
// that is delegated to a validator that has not voted for five epochs.  So, if it has been deactivated, this
// instruction delegates it to Shinobi Systems again, once the stake program would allow it (see
// is_stake_delegatable_after_deactivation in util_stake.c).
//
// It is permissionless: anyone may run it, at any time, any number of times.  It can do only what the program would
// always want done, and its result does not depend on who runs it or when.  Anyone who levels up has a reason to run
// it, since real reward rates generally fall, and a lower rate gives more level credits for the same Ki; such a user
// can run it in the same transaction as LevelUp.  When rates rise, levellers would rather not run it, but anyone else
// can.  LevelUp never requires it: it uses the most recent rate, so if this instruction is not run, or cannot measure,
// LevelUp keeps working, with the rate merely going stale.
//
// Whether it fails when it does not measure is chosen by its caller (see require_measured below), so that a user who
// includes it in the same transaction as LevelUp can choose either to level up only with a rate that is up to date
// for the window, or to level up regardless.

typedef struct
{
    // This is the instruction code for MeasureRewardRate
    uint8_t instruction_code;

    // If this is 0, then the instruction succeeds even if the reward rate has not been measured in the current level
    // credit window and cannot be measured now; it then does whatever else it can (taking a snapshot, or delegating
    // the reference stake account again), and the reward rate is left as it was.  If this is 1, then in that case the
    // instruction fails with Error_RewardRateNotMeasured, which fails the whole transaction, including anything else
    // that it does.  Either way, it succeeds if the rate has already been measured in the current window.  No other
    // value is allowed.
    //
    // Note that with this set, a failure also discards a snapshot or delegation that the instruction would otherwise
    // have made, so it is not suitable for keeping the rate up to date on its own.
    uint8_t require_measured;

} MeasureRewardRateData;


// The size of the EpochRewards sysvar's data, and the offset within it of its "active" flag.  The data is the bincode
// serialization of: distribution_starting_block_height (u64), num_partitions (u64), parent_blockhash (32 bytes),
// total_points (u128), total_rewards (u64), distributed_rewards (u64), and active (bool).
#define EPOCH_REWARDS_SYSVAR_DATA_LEN 81
#define EPOCH_REWARDS_SYSVAR_ACTIVE_OFFSET 80


// Returns true if the staking rewards of the previous epoch may still be being paid.  They are paid at the start of
// each epoch, a partition of stake accounts per block, over the first blocks of the epoch; until that is done, some
// stake accounts have been paid and others have not.  The EpochRewards sysvar's active flag says whether that is
// still under way, and is used whenever the sysvar can be read.  If it cannot be (it does not exist, or is not in the
// expected format, for example after some future change to the way that rewards are paid), then rewards are taken to
// be being paid for the first EPOCH_REWARDS_FALLBACK_SECONDS of the epoch, which is far longer than they take.
static bool are_epoch_rewards_being_paid(const SolAccountInfo *epoch_rewards_sysvar_account, const Clock *clock)
{
    if (epoch_rewards_sysvar_account->data_len == EPOCH_REWARDS_SYSVAR_DATA_LEN) {
        uint8_t active = epoch_rewards_sysvar_account->data[EPOCH_REWARDS_SYSVAR_ACTIVE_OFFSET];
        // A bool is only ever 0 or 1; anything else means the format is not the one expected
        if (active <= 1) {
            return (active == 1);
        }
    }

    // The epoch's start time can't be later than the current time, but be certain that a clock that has stepped
    // backwards cannot make the time into the epoch look large
    if (clock->unix_timestamp <= clock->epoch_start_timestamp) {
        return true;
    }
    return (((uint64_t) (clock->unix_timestamp - clock->epoch_start_timestamp)) < EPOCH_REWARDS_FALLBACK_SECONDS);
}


// Records a new snapshot of the reference stake account: its delegated stake [stake] as of the current epoch
static void take_reference_stake_snapshot(ProgramState *program_state, uint64_t stake, const Clock *clock)
{
    program_state->snapshot_stake = stake;
    program_state->snapshot_epoch = clock->epoch;
    program_state->snapshot_epoch_start_timestamp = clock->epoch_start_timestamp;
}


static uint64_t anyone_measure_reward_rate(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts.  The authority, vote account,
    // stake history, stake config, and stake program are needed only to delegate the reference stake account again
    // after it has been deactivated, but are always required, so that the instruction's accounts never depend on the
    // state of the reference stake account.
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,  state_account,                 ReadWrite,  NotSigner,  KnownAccount_ProgramState);
        DECLARE_ACCOUNT(1,  reference_stake_account,       ReadWrite,  NotSigner,  KnownAccount_ReferenceStake);
        DECLARE_ACCOUNT(2,  authority_account,             ReadOnly,   NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(3,  clock_sysvar_account,          ReadOnly,   NotSigner,  KnownAccount_ClockSysvar);
        DECLARE_ACCOUNT(4,  stake_history_sysvar_account,  ReadOnly,   NotSigner,  KnownAccount_StakeHistorySysvar);
        DECLARE_ACCOUNT(5,  epoch_rewards_sysvar_account,  ReadOnly,   NotSigner,  KnownAccount_EpochRewardsSysvar);
        DECLARE_ACCOUNT(6,  shinobi_systems_vote_account,  ReadOnly,   NotSigner,  KnownAccount_ShinobiSystemsVote);
        DECLARE_ACCOUNT(7,  stake_config_account,          ReadOnly,   NotSigner,  KnownAccount_StakeConfig);
        DECLARE_ACCOUNT(8,  stake_program_account,         ReadOnly,   NotSigner,  KnownAccount_StakeProgram);
    }
    DECLARE_ACCOUNTS_NUMBER(9);

    // Ensure that the instruction data is the correct size
    if (params->data_len != sizeof(MeasureRewardRateData)) {
        return Error_InvalidDataSize;
    }
    const MeasureRewardRateData *data = (const MeasureRewardRateData *) params->data;

    // require_measured must be 0 or 1
    if (data->require_measured > 1) {
        return Error_InvalidData_First;
    }

    // The program state account, which is at its fixed address (checked above)
    ProgramState *program_state = (ProgramState *) state_account->data;

    // Get the clock sysvar, needed below
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }
    // The cluster's clock is never before 1970, but don't allow a negative timestamp to be converted into a huge
    // unsigned window number
    if (clock.unix_timestamp < 0) {
        return Error_FailedToGetClock;
    }

    // The current level credit window.  A clock that has stepped backwards is clamped to the window of the last
    // measurement, so that it can never be used to measure twice in one window (see compute_level_credit_window).
    uint64_t window = compute_level_credit_window((uint64_t) clock.unix_timestamp, program_state->reward_rate_window,
                                                  LEVEL_CREDIT_WINDOW_SECONDS);

    // If the rate has already been measured in this window (or set by Initialize in it), there is nothing more to
    // do; the rate is up to date for the window
    if (program_state->reward_rate_window == window) {
        return 0;
    }

    // From here on, every outcome other than a measurement is "not measured", which fails only if the caller asked
    // for that
    uint64_t not_measured = data->require_measured ? Error_RewardRateNotMeasured : 0;

    // Nothing can be done while the current epoch's rewards may still be being paid: a snapshot or measurement could
    // be made from a delegated stake that does not yet include the epoch's rewards.  (The stake program would refuse to
    // delegate the reference stake account then, too.)
    if (are_epoch_rewards_being_paid(epoch_rewards_sysvar_account, &clock)) {
        return not_measured;
    }

    // Decode the reference stake account.  It is always a stake account; if it cannot be decoded, the stake program's
    // format must have changed, and nothing can be measured.  LevelUp keeps using the most recent rate.
    Stake stake;
    if (!decode_stake_account(reference_stake_account, &stake)) {
        return not_measured;
    }

    // The reference stake account is earning rewards at Shinobi Systems if it is delegated there and not deactivated.
    // It is only ever delegated to Shinobi Systems; the vote account is checked anyway, since a measurement from any
    // other delegation would be meaningless.
    bool earning = ((stake.state == StakeState_Stake) &&
                    is_shinobi_systems_vote_account(&(stake.stake.delegation.voter_pubkey)) &&
                    (stake.stake.delegation.deactivation_epoch == UINT64_MAX));

    if (!earning) {
        // Discard the snapshot, so that no measurement spans the time in which the reference stake account is not
        // earning; a new snapshot is taken once it is earning again
        program_state->snapshot_stake = 0;

        // Delegate it to Shinobi Systems again if the stake program will allow it: if it was never delegated (which
        // Initialize always does, so this is not expected), or if it was deactivated and is no longer cooling down.
        // The delegation is signed by the authority, which is its stake authority.  The stake program delegates all of
        // its lamports beyond its rent exempt reserve.
        if ((stake.state == StakeState_Initialized) ||
            ((stake.state == StakeState_Stake) && (stake.stake.delegation.deactivation_epoch != UINT64_MAX) &&
             is_stake_delegatable_after_deactivation(&stake, stake_history_sysvar_account, clock.epoch))) {
            delegate_stake_signed(reference_stake_account->key, &(Constants.shinobi_systems_vote_pubkey), params->ka,
                                  params->ka_num);
        }

        return not_measured;
    }

    // The first epoch in which the delegation earns rewards is the one after its activation epoch (the epoch in which
    // it was delegated).  The stake program uses an activation epoch of UINT64_MAX for stake that was active from the
    // cluster's genesis, which earns in every epoch.
    uint64_t activation_epoch = stake.stake.delegation.activation_epoch;
    uint64_t first_earning_epoch = (activation_epoch == UINT64_MAX) ? 0 : (activation_epoch + 1);

    // If it is still warming up, it has not earned anything yet, and there is no snapshot to take: a snapshot must be
    // taken in an epoch in which it is earning, so that all of the epochs that a measurement from it covers are
    // epochs in which it earned
    if (clock.epoch < first_earning_epoch) {
        return not_measured;
    }

    uint64_t delegated_stake = stake.stake.delegation.stake;

    // Take a new snapshot, rather than measuring, if there is none; if the snapshot was taken before the delegation
    // started earning (it was delegated again since the snapshot); or if the delegated stake has fallen since the
    // snapshot, which staking rewards cannot do.  These are not expected to happen except for the first, but none of
    // them can give a valid measurement.
    if (!program_state->snapshot_stake || (program_state->snapshot_epoch < first_earning_epoch) ||
        (delegated_stake < program_state->snapshot_stake)) {
        take_reference_stake_snapshot(program_state, delegated_stake, &clock);
        return not_measured;
    }

    // If no epoch has started since the snapshot, then there are no rewards to measure yet
    if (clock.epoch <= program_state->snapshot_epoch) {
        return not_measured;
    }

    // Epochs start later than earlier epochs; but if the cluster's clock says otherwise, nothing sensible can be
    // measured, so start again from a new snapshot
    if (clock.epoch_start_timestamp <= program_state->snapshot_epoch_start_timestamp) {
        take_reference_stake_snapshot(program_state, delegated_stake, &clock);
        return not_measured;
    }

    // Measure the rate: the growth of the delegated stake since the snapshot, which is the rewards of every epoch from
    // the snapshot's epoch up to but not including the current one, over the time from the start of the snapshot's
    // epoch to the start of the current one
    uint64_t seconds = (uint64_t) (clock.epoch_start_timestamp - program_state->snapshot_epoch_start_timestamp);
    uint64_t measured_rate = compute_measured_reward_rate(program_state->snapshot_stake, delegated_stake, seconds);

    // Limit, smooth, and bound it, and record it, along with the window in which it was measured
    program_state->reward_rate = compute_updated_reward_rate(program_state->reward_rate, measured_rate);
    program_state->reward_rate_window = window;

    // The next measurement is made from now
    take_reference_stake_snapshot(program_state, delegated_stake, &clock);

    return 0;
}
