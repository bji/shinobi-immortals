#pragma once

#include "util/util_calculations.c"

typedef struct
{
    uint8_t instruction_code;

    uint64_t ki_to_apply;
} LevelUpData;


static uint64_t user_level_up(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,   entry_account,                    ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(1,   token_owner_account,              ReadOnly,   Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,   token_account,                    ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(3,   entry_metadata_account,           ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(4,   ki_source_account,                ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(5,   ki_source_owner_account,          ReadOnly,   Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(6,   ki_mint_account,                  ReadWrite,  NotSigner,  KnownAccount_KiMint);
        DECLARE_ACCOUNT(7,   authority_account,                ReadOnly,   NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(8,   spl_token_program_account,        ReadOnly,   NotSigner,  KnownAccount_SPLTokenProgram);
        DECLARE_ACCOUNT(9,   metaplex_program_account,         ReadOnly,   NotSigner,  KnownAccount_MetaplexProgram);
    }
    DECLARE_ACCOUNTS_NUMBER(10);

    // Ensure that the instruction data is the correct size
    if (params->data_len != sizeof(LevelUpData)) {
        return Error_InvalidDataSize;
    }
    const LevelUpData *data = (const LevelUpData *) params->data;

    // This is the entry data
    Entry *entry = get_validated_entry(entry_account);
    if (!entry) {
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
    switch (get_entry_state(0, entry, &clock)) {
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

    // Check to make sure that the entry token is owned by the token owner account
    if (!is_token_owner(token_account, token_owner_account->key, &(entry->mint_pubkey), 1)) {
        return Error_InvalidAccount_First + 2;
    }
    // Check to make sure that the correct entry metadata account was passed in
    if (!SolPubkey_same(&(entry->metaplex_metadata_pubkey), entry_metadata_account->key)) {
        return Error_InvalidAccount_First + 3;
    }

    // Contributions must be positive and use a fixed level-zero ki_factor.
    // Changing levels must not change the curve within a window or retroactively
    // change the credit attributed to earlier contributions in that window.
    if (!data->ki_to_apply || !entry->metadata.level_1_credits ||
        !entry->metadata.level_metadata[0].ki_factor || !entry->metadata.expected_annual_reward_bps) {
        return Error_InvalidData_First;
    }

    // Reset only the window's raw contribution counter; lifetime credits persist.
    uint64_t previous_ki = ((entry->most_recent_ki_applied_window == window) ? entry->ki_applied_this_window : 0);
    bool overflow = false;
    uint64_t old_credits = compute_level_credits(entry->metadata.level_1_credits,
                                                 entry->metadata.level_metadata[0].ki_factor,
                                                 entry->metadata.expected_annual_reward_bps,
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
                                                         entry->metadata.expected_annual_reward_bps,
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
                                                 entry->metadata.expected_annual_reward_bps,
                                                 new_ki, LEVEL_CREDIT_WINDOWS_PER_YEAR, &overflow);
    if (overflow || (new_credits < old_credits)) {
        return Error_InvalidData_First;
    }
    uint64_t total_credits = checked_add(entry->total_level_credits, new_credits - old_credits, &overflow);
    if (overflow) {
        return Error_InvalidData_First;
    }

    // The number of mint Tokens to actually use is 10 times the number of Ki, because the Mint stores 10 mint tokens
    // per 1 Ki due to the fact that it has 1 decimal place.  Only the (possibly limited) amount actually applied is
    // burned, and only that amount needs to be present in the source account.
    uint64_t ki_to_burn = checked_multiply(ki_to_apply, 10, &overflow);
    if (overflow) {
        ki_to_burn = UINT64_MAX;
    }

    // Check to make sure that the ki source account passed in is really a ki account owned by the passed in owner
    // and has the required amount of Ki
    if (!is_token_owner(ki_source_account, ki_source_owner_account->key, &(Constants.ki_mint_pubkey), ki_to_burn)) {
        return Error_InvalidAccount_First + 5;
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
