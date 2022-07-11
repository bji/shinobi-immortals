#pragma once

#include "util/util_commission.c"
#include "util/util_ki.c"
#include "util/util_stake.c"


// Account references:
// 0. `[WRITE]` -- The account to use for temporary funding operations
// 1. `[]` -- The block account address
// 2. `[WRITE]` -- The entry account address
// 3. `[SIGNER]` -- The entry token owner account
// 4. `[]` -- The entry token account
// 5. `[WRITE]` -- The stake account
// 6. `[]` -- The new authority for the stake account
// 7. `[WRITE]` -- The destination account for harvested Ki
// 8. `[]` -- The system account that will own the destination ki token account if it needs to be created; or
//             currently owns it if it's already created
// 9. `[WRITE]` -- The master stake account
// 10. `[WRITE]` -- The bridge stake account
// 11. `[]` -- The nifty authority account (for commission charge splitting/merging)
// 12. `[]` -- Clock sysvar (required by stake program)
// 13. `[]` -- The system program id (for cross-program invoke)
// 14. `[]` -- The stake program id (for cross-program invoke)
// 15. `[]` -- The SPL associated token account program, for cross-program invoke

typedef struct
{
    // This is the instruction code for Buy
    uint8_t instruction_code;

    // This is the entry_index of the entry within its block
    uint16_t entry_index;

    // This is the bump seed needed to compute the bridge stake account address for the entry
    uint8_t bridge_bump_seed;

    // This is the minimum number of lamports allowed to be staked in a stake account.  It's possible that stake
    // accounts may have minimum stake amounts in the future; this allows the minimum to be specified.  If this is too
    // low of a value, then the transaction may fail since the bridge account creation during commission charge may
    // fail.  If this is too high of a value (i.e. this number of lamports is not available in the master stake
    // account), then the transaction may fail because splitting this amount out for bridging would fail.
    uint64_t minimum_stake_lamports;
    
} DestakeData;


static uint64_t user_destake(SolParameters *params)
{
    // Sanitize the accounts.  There must be exactly 16.
    if (params->ka_num != 16) {
        return Error_IncorrectNumberOfAccounts;
    }

    SolAccountInfo *funding_account = &(params->ka[0]);
    SolAccountInfo *block_account = &(params->ka[1]);
    SolAccountInfo *entry_account = &(params->ka[2]);
    SolAccountInfo *entry_token_owner_account = &(params->ka[3]);
    SolAccountInfo *entry_token_account = &(params->ka[4]);
    SolAccountInfo *stake_account = &(params->ka[5]);
    SolAccountInfo *new_withdraw_authority_account = &(params->ka[6]);
    SolAccountInfo *ki_destination_account = &(params->ka[7]);
    SolAccountInfo *ki_destination_owner_account = &(params->ka[8]);
    SolAccountInfo *master_stake_account = &(params->ka[9]);
    SolAccountInfo *bridge_stake_account = &(params->ka[10]);
    SolAccountInfo *authority_account = &(params->ka[11]);
    SolAccountInfo *clock_sysvar_account = &(params->ka[12]);
    // The account at index 13 must be the system program, but this is not checked; if it's not that program, then
    // the transaction will simply fail when it is used to sign a cross-program invoke later
    // The account at index 14 must be the stake program, but this is not checked; if it's not that program, then
    // the transaction will simply fail when it is used to sign a cross-program invoke later
    // The account at index 15 must be the SPL associated token account program, but this is not checked; if it's not
    // that program, then the transaction will simply fail when it is used to sign a cross-program invoke later

    // Make sure that the input data is the correct size
    if (params->data_len != sizeof(DestakeData)) {
        return Error_InvalidDataSize;
    }
    
    // Cast to instruction data
    DestakeData *data = (DestakeData *) params->data;
    
    // Check permissions
    if (!funding_account->is_writable) {
        return Error_InvalidAccountPermissions_First;
    }
    if (!entry_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 2;
    }
    if (!entry_token_owner_account->is_signer) {
        return Error_InvalidAccountPermissions_First + 3;
    }
    if (!stake_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 5;
    }
    if (!ki_destination_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 7;
    }
    if (!master_stake_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 8;
    }
    if (!bridge_stake_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 9;
    }

    // Get validated block and entry, which checks all validity of those accounts
    Block *block = get_validated_block(block_account);
    if (!block) {
        return Error_InvalidAccount_First;
    }

    // Ensure that the block is complete; cannot stake in a block that is not complete yet
    if (!is_block_complete(block)) {
        return Error_BlockNotComplete;
    }

    // This is the entry data
    Entry *entry = get_validated_entry(block, data->entry_index, entry_account);
    if (!entry) {
        return Error_InvalidAccount_First + 2;
    }

    // Get the clock sysvar, needed below
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }

    // Check to make sure that the entry is staked
    if (get_entry_state(block, entry, &clock) != EntryState_OwnedAndStaked) {
        return Error_NotStakeable;
    }

    // Check to make sure that the entry token is owned by the token owner account
    if (!is_token_owner(entry_token_account, entry_token_owner_account->key, &(entry->mint_account.address))) {
        return Error_InvalidAccount_First + 4;
    }

    // Check to make sure that the stake account passed in is actually staked in the entry
    if (!SolPubkey_same(&(entry->staked.stake_account), stake_account->key)) {
        return Error_InvalidAccount_First + 5;
    }

    // Check to make sure that the master stake account passed in is actually the master stake account
    if (!is_master_stake_account(master_stake_account->key)) {
        return Error_InvalidAccount_First + 8;
    }

    // Check to make sure that the proper bridge account was passed in
    SolPubkey bridge_pubkey;
    if (!compute_bridge_address(block->config.group_number, block->config.block_number, entry->entry_index,
                                data->bridge_bump_seed, &bridge_pubkey) ||
        !SolPubkey_same(&bridge_pubkey, bridge_stake_account->key)) {
        return Error_InvalidAccount_First + 9;
    }

    // Check to make sure that the proper nifty program authority account was passed in
    if (!is_nifty_authority_account(authority_account->key)) {
        return Error_InvalidAccount_First + 10;
    }

    // Check to make sure that the clock sysvar account really is a reference to that account
    if (!SolPubkey_same(&(Constants.clock_sysvar_pubkey), clock_sysvar_account->key)) {
        return Error_InvalidAccount_First + 12;
    }

    // Decode the stake account
    Stake stake;
    if (!decode_stake_account(stake_account, &stake)) {
        return Error_InvalidAccount_First + 5;
    }

    // Harvest Ki.  Must be done before commission is charged since commission charge actually reduces the number of
    // lamports in the stake account, which would affect Ki harvest calculations
    uint64_t ret = harvest_ki(&stake, entry, ki_destination_account, ki_destination_owner_account->key,
                              params->ka, params->ka_num);
    if (ret) {
        return ret;
    }

    // Charge commission
    ret = charge_commission(&stake, block, entry, funding_account->key, bridge_stake_account, data->bridge_bump_seed,
                            stake_account->key, data->minimum_stake_lamports, params->ka, params->ka_num);
    if (ret) {
        return ret;
    }

    // Use stake account program to set all authorities to the new authority
    if (!set_stake_authorities(stake_account->key, &(Constants.nifty_authority_pubkey),
                               new_withdraw_authority_account->key, params->ka, params->ka_num)) {
        return Error_SetStakeAuthoritiesFailed;
    }

    // No longer staked
    sol_memset(&(entry->staked), 0, sizeof(entry->staked));
    
    return 2000;
}
