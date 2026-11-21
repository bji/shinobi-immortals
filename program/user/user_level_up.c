#pragma once

#include "inc/program_state.h"
#include "util/util_calculations.c"
#include "util/util_ki.c"
#include "util/util_stake.c"

typedef struct
{
    uint8_t instruction_code;

    // The most whole Ki to apply.  Only as much of it as earns level credits is burned: it is reduced to what is
    // needed to reach level 9, and to the least that earns the same level credits (see below).
    uint64_t ki_to_apply;
} LevelUpData;


static uint64_t user_level_up(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        // The entry's block.  If the entry is staked, its block_start_timestamp is the beginning of the entry's life
        // span, over which the Ki that is harvested is reduced.  It comes right before the entry, as in other
        // instructions that take both.
        DECLARE_ACCOUNT(0,   block_account,                    ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(1,   entry_account,                    ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,   token_owner_account,              ReadOnly,   Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(3,   token_account,                    ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(4,   entry_metadata_account,           ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(5,   ki_source_account,                ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        // The Ki source owner pays for the creation of the Ki source account, if Ki is harvested into it and it does
        // not exist yet (see below), so it must be writable
        DECLARE_ACCOUNT(6,   ki_source_owner_account,          ReadWrite,  Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(7,   ki_mint_account,                  ReadWrite,  NotSigner,  KnownAccount_KiMint);
        DECLARE_ACCOUNT(8,   authority_account,                ReadOnly,   NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(9,   spl_token_program_account,        ReadOnly,   NotSigner,  KnownAccount_SPLTokenProgram);
        DECLARE_ACCOUNT(10,  metaplex_program_account,         ReadOnly,   NotSigner,  KnownAccount_MetaplexProgram);
        // The entry's stake account if the entry is staked; any account otherwise, since it is then not used
        DECLARE_ACCOUNT(11,  stake_account,                    ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        // These two are needed to create the Ki source account as the Ki source owner's associated Ki token account,
        // if Ki is harvested into it and it does not exist yet
        DECLARE_ACCOUNT(12,  system_program_account,           ReadOnly,   NotSigner,  KnownAccount_SystemProgram);
        DECLARE_ACCOUNT(13,  spl_ata_program_account,          ReadOnly,   NotSigner,  KnownAccount_SPLATAProgram);
        // The program state, which holds the reward rate that calibrates the entry's level credit curve (see
        // MeasureRewardRate).  It is only read, so that level ups do not contend with one another for it.
        DECLARE_ACCOUNT(14,  state_account,                    ReadOnly,   NotSigner,  KnownAccount_ProgramState);
    }
    DECLARE_ACCOUNTS_NUMBER(15);

    // Ensure that the instruction data is the correct size
    if (params->data_len != sizeof(LevelUpData)) {
        return Error_InvalidDataSize;
    }
    const LevelUpData *data = (const LevelUpData *) params->data;

    // This is the entry data, which must be an entry of the given block
    Entry *entry = get_validated_entry_of_block(entry_account, block_account->key);
    if (!entry) {
        return Error_InvalidAccount_First + 1;
    }

    // This is the block data
    const Block *block = get_validated_block(block_account);
    if (!block) {
        return Error_InvalidAccount_First;
    }

    // Get the clock sysvar, needed below
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }
    // The current level credit window, numbered from the Unix epoch.  The cluster's clock is never before 1970,
    // but don't allow a negative timestamp to be converted into a huge unsigned window number.
    if (clock.unix_timestamp < 0) {
        return Error_FailedToGetClock;
    }
    // A clock that has stepped backwards is clamped to the entry's recorded window, so that it can never be used to
    // obtain a fresh credit curve (see compute_level_credit_window)
    uint64_t window = compute_level_credit_window((uint64_t) clock.unix_timestamp,
                                                  entry->most_recent_ki_applied_window, LEVEL_CREDIT_WINDOW_SECONDS);

    // Check to make sure that the entry is Owned or OwnedAndStaked
    EntryState entry_state = get_entry_state(0, entry, &clock);
    switch (entry_state) {
    case EntryState_Owned:
    case EntryState_OwnedAndStaked:
        break;
    default:
        return Error_NotOwned;
    }

    // Check to make sure that the entry is not already at level index 8 (which is known to the user as level 9)
    if (entry->level == 8) {
        return Error_AlreadyAtMaxLevel;
    }

    // Check to make sure that the entry token is owned by the token owner account.  The entry may be staked, in which
    // case its token account is frozen.
    if (!is_token_owner(token_account, token_owner_account->key, &(entry->mint_pubkey), 1, true)) {
        return Error_InvalidAccount_First + 3;
    }
    // Check to make sure that the correct entry metadata account was passed in
    if (!SolPubkey_same(&(entry->metaplex_metadata_pubkey), entry_metadata_account->key)) {
        return Error_InvalidAccount_First + 4;
    }

    // If the entry is staked, harvest the Ki that its stake account has earned since the last harvest, before this
    // instruction can change the entry's level.  harvest_ki pays all unharvested rewards at the ki_factor of the
    // entry's current level, so if the level changed first, rewards earned at the old level would be paid at the new
    // level's ki_factor.  Harvesting here means rewards are always paid at the level at which they were earned.
    //
    // The harvested Ki goes to the Ki source account, which must be a Ki token account of the Ki source owner (who
    // has signed), or else the Ki source owner's associated Ki token account, which is created (paid for by the Ki
    // source owner) if Ki is harvested into it and it does not exist yet.  So a user who has never held Ki can level
    // up a staked entry using only the Ki that it has earned, in a single LevelUp.  The token owner, who has also
    // signed, thereby chooses where the Ki goes, just as with Harvest.  Since it is minted before the Ki to apply is
    // burned, freshly harvested Ki can be applied by this same instruction.  If no level change results, nothing is
    // lost: the Ki is harvested as Harvest would have.
    if (entry_state == EntryState_OwnedAndStaked) {
        // Check to make sure that the stake account passed in is actually staked in the entry
        if (!SolPubkey_same(&(entry->owned.stake_account), stake_account->key)) {
            return Error_InvalidAccount_First + 11;
        }

        // Decode the stake account
        Stake stake;
        if (!decode_stake_account(stake_account, &stake)) {
            return Error_InvalidAccount_First + 11;
        }

        // Check that the Ki source account is either already a Ki token account of the Ki source owner, or else the
        // Ki source owner's associated Ki token account, which harvest_ki creates if it needs to and it does not
        // exist.  Checking this here, rather than leaving it to harvest_ki, means that any other account is refused
        // with this program's own error, rather than with whatever error the Associated Token Account program would
        // give when asked to create an account at the wrong address.
        if (!is_token_owner(ki_source_account, ki_source_owner_account->key, &(Constants.ki_mint_pubkey), 0,
                            false)) {
            SolPubkey associated_ki_account;
            if (!compute_associated_token_address(ki_source_owner_account->key, &(Constants.ki_mint_pubkey),
                                                  &associated_ki_account) ||
                !SolPubkey_same(ki_source_account->key, &associated_ki_account)) {
                return Error_InvalidAccount_First + 5;
            }
        }

        harvest_ki(&stake, block, entry, ki_source_account, ki_source_owner_account->key,
                   ki_source_owner_account->key, &clock, params->ka, params->ka_num);
    }

    // The annual reward rate, in basis points, that calibrates the level credit curve: the program state's reward
    // rate (set by Initialize, and updated by MeasureRewardRate), rounded to whole basis points, which is the same for
    // every entry of every block.  It is always at least REWARD_RATE_MIN (100 basis points), so is never zero.  If
    // MeasureRewardRate measures a new rate partway through a window, the curve changes for the rest of the window;
    // the credits of the window are then those of the old curve up to the Ki applied before the change, plus those of
    // the new curve from there, which differs from the new curve alone only by the gap between the two curves at that
    // point.  Rates move by at most 1.25% per window, so that is negligible.
    const ProgramState *program_state = (const ProgramState *) state_account->data;
    uint32_t reward_bps = compute_reward_rate_bps(program_state->reward_rate);

    // Contributions must be positive and use a fixed level-zero ki_factor.
    // Changing levels must not change the curve within a window or retroactively
    // change the credit attributed to earlier contributions in that window.
    if (!data->ki_to_apply || !entry->metadata.level_1_credits ||
        !entry->metadata.level_metadata[0].ki_factor) {
        return Error_InvalidData_First;
    }

    // Reset only the window's raw contribution counter; lifetime credits persist.
    uint64_t previous_ki = ((entry->most_recent_ki_applied_window == window) ? entry->ki_applied_this_window : 0);
    bool overflow = false;
    uint64_t old_credits = compute_level_credits(entry->metadata.level_1_credits,
                                                 entry->metadata.level_metadata[0].ki_factor,
                                                 reward_bps,
                                                 previous_ki, LEVEL_CREDIT_WINDOWS_PER_YEAR, &overflow);
    if (overflow) {
        return Error_InvalidData_First;
    }

    // Never burn more Ki than is needed to reach the maximum level.  The credits still needed are the level 9
    // requirement less the credits already earned; the window curve must therefore reach old_credits plus that
    // amount.  Find the smallest window Ki total that does so, and limit this contribution to the difference between
    // that and what the window has already received.  The entry is below level 9 (checked above), so it still needs
    // credits; and since metadata cannot change after reveal, total_level_credits is below the level 9 requirement.
    uint64_t max_level_requirement = compute_level_requirement(entry->metadata.level_1_credits, 8);
    uint64_t ki_to_apply = data->ki_to_apply;
    if (entry->total_level_credits >= max_level_requirement) {
        // Defensive only: there is nothing left to earn, so no Ki would be burned
        return Error_AlreadyAtMaxLevel;
    }
    uint64_t target_credits = checked_add(old_credits, max_level_requirement - entry->total_level_credits, &overflow);
    if (overflow) {
        return Error_InvalidData_First;
    }
    uint64_t ki_needed = compute_level_credits_ki_needed(entry->metadata.level_1_credits,
                                                         entry->metadata.level_metadata[0].ki_factor,
                                                         reward_bps,
                                                         target_credits, LEVEL_CREDIT_WINDOWS_PER_YEAR);
    // UINT64_MAX means the remaining credits cannot all be earned in this window, so no limit applies.  Otherwise
    // ki_needed > previous_ki, because previous_ki only reaches old_credits, which is below the target.
    if ((ki_needed != UINT64_MAX) && (ki_needed > previous_ki) && (ki_to_apply > (ki_needed - previous_ki))) {
        ki_to_apply = ki_needed - previous_ki;
    }
    // Fail rather than burn nothing
    if (!ki_to_apply) {
        return Error_AlreadyAtMaxLevel;
    }

    uint64_t new_ki = checked_add(previous_ki, ki_to_apply, &overflow);
    uint64_t new_credits = compute_level_credits(entry->metadata.level_1_credits,
                                                 entry->metadata.level_metadata[0].ki_factor,
                                                 reward_bps,
                                                 new_ki, LEVEL_CREDIT_WINDOWS_PER_YEAR, &overflow);
    if (overflow || (new_credits < old_credits)) {
        return Error_InvalidData_First;
    }
    // Never burn Ki that earns nothing.  The curve is rounded down to whole tenths of a credit, so a contribution that
    // is too small (or that is made when the window's curve is already nearly flat) can leave the window's credits
    // unchanged.  Reject it rather than burn the Ki; the user can apply a larger amount, or wait for the next window.
    if (new_credits == old_credits) {
        return Error_NoLevelCreditsEarned;
    }

    // Never burn Ki beyond the least that earns new_credits.  The window's curve is rounded down to whole tenths of a
    // credit, so every window Ki total from the smallest one that reaches new_credits up to new_ki earns exactly the
    // same credits; any Ki beyond that smallest total would be burned for nothing.  This is always the case, to some
    // degree, for a contribution that does not happen to land exactly on a step of the curve; and it can be a great
    // deal of Ki for a large contribution, since the curve's credits stop increasing at all past some window Ki total
    // (they approach, but never reach, the window's maximum).  The level 9 limit above only covers the window in which
    // level 9 can be reached; this covers every window.  It finds the smallest window Ki total whose credits are at
    // least new_credits, which, because the curve never decreases, is the smallest whose credits are exactly
    // new_credits.  It exceeds previous_ki, which only reaches old_credits, below new_credits, and is no more than
    // new_ki, which reaches new_credits.  The contribution is reduced so that the window total is that smallest total.
    // The credits earned are unchanged; only the Ki burned, and the window's recorded Ki total, are reduced.
    {
        uint64_t least_ki = compute_level_credits_ki_needed(entry->metadata.level_1_credits,
                                                            entry->metadata.level_metadata[0].ki_factor,
                                                            reward_bps,
                                                            new_credits, LEVEL_CREDIT_WINDOWS_PER_YEAR);
        // Only reduce the contribution if the smallest total is strictly between what the window had already received
        // and the total that this contribution would make.  UINT64_MAX (no answer) is never below new_ki.
        if ((least_ki > previous_ki) && (least_ki < new_ki)) {
            // Confirm with the real credit calculation that the smaller total earns exactly the same credits, so that
            // the reduction can never change the credits awarded.  compute_level_credits_ki_needed already guarantees
            // this; it is checked again here because getting it wrong would cost the user credits.
            bool least_overflow = false;
            uint64_t least_credits = compute_level_credits(entry->metadata.level_1_credits,
                                                           entry->metadata.level_metadata[0].ki_factor,
                                                           reward_bps,
                                                           least_ki, LEVEL_CREDIT_WINDOWS_PER_YEAR, &least_overflow);
            if (!least_overflow && (least_credits == new_credits)) {
                ki_to_apply = least_ki - previous_ki;
                new_ki = least_ki;
            }
        }
    }

    uint64_t total_credits = checked_add(entry->total_level_credits, new_credits - old_credits, &overflow);
    if (overflow) {
        return Error_InvalidData_First;
    }

    // The number of mint Tokens to actually use is 10 times the number of Ki, because the Mint stores 10 mint tokens
    // per 1 Ki due to the fact that it has 1 decimal place.  Only the amount actually applied, which may have been
    // reduced above (to what is needed to reach level 9, and to the least that earns the credits awarded), is burned,
    // and only that amount needs to be present in the source account.
    uint64_t ki_to_burn = checked_multiply(ki_to_apply, 10, &overflow);
    if (overflow) {
        ki_to_burn = UINT64_MAX;
    }

    // Check to make sure that the ki source account passed in is really a ki account owned by the passed in owner.
    // If the entry is staked and the account did not exist, harvest_ki created it if it harvested any Ki; if it
    // harvested none, the account still does not exist.  An account that does not exist holds no Ki, so that is
    // reported as insufficient Ki rather than as a bad account, since that is the actual problem.
    if (!is_token_owner(ki_source_account, ki_source_owner_account->key, &(Constants.ki_mint_pubkey), 0, false)) {
        if ((ki_source_account->data_len == 0) && is_system_program(ki_source_account->owner)) {
            return Error_InsufficientFunds;
        }
        return Error_InvalidAccount_First + 5;
    }

    // And check that it holds the Ki to burn
    if (!is_token_owner(ki_source_account, ki_source_owner_account->key, &(Constants.ki_mint_pubkey), ki_to_burn,
                        false)) {
        return Error_InsufficientFunds;
    }

    // Burn the Ki
    burn_tokens(ki_source_account->key, ki_source_owner_account->key,
                &(Constants.ki_mint_pubkey), ki_to_burn, params->ka, params->ka_num);

    entry->most_recent_ki_applied_window = window;
    entry->ki_applied_this_window = new_ki;
    entry->total_level_credits = total_credits;

    // One contribution can cross multiple cumulative thresholds. It can also
    // earn credits without changing level; update metadata only when needed.
    uint8_t previous_level = entry->level;
    while ((entry->level < 8) &&
           (total_credits >= compute_level_requirement(entry->metadata.level_1_credits, entry->level + 1))) {
        entry->level++;
    }
    if (entry->level != previous_level) {
        return set_metaplex_metadata_for_level(entry, entry->level, entry_metadata_account, params->ka, params->ka_num);
    }
    return 0;
}
