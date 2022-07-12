#pragma once

// Account references:
// 0. `[WRITE]` -- The account to use for funding operations
// 1. `[]` -- The block account address
// 2. `[WRITE]` -- The entry account address
// 3. `[SIGNER]` -- The entry token owner account
// 4. `[]` -- The entry token account
// 5. `[]` -- The stake account
// 6. `[WRITE]` -- The destination account for harvested Ki
// 7. `[]` -- The system account that will own the destination ki token account if it needs to be created; or
//             currently owns it if it's already created
// 8. `[WRITE]` -- The Ki mint account
// 9. `[]` -- The nifty authority account (for commission charge splitting/merging)
// 10. `[]` -- The system program
// 11. `[]` -- The SPL Token account program
// 12. `[]` -- The SPL associated token account program, for cross-program invoke

typedef struct
{
    // This is the instruction code for Harvest
    uint8_t instruction_code;

    // This is the entry_index of the entry within its block
    uint16_t entry_index;
    
} HarvestData;


static uint64_t user_harvest(SolParameters *params)
{
    // Sanitize the accounts.  There must be exactly 13.
    if (params->ka_num != 13) {
        return Error_IncorrectNumberOfAccounts;
    }

    SolAccountInfo *funding_account = &(params->ka[0]);
    SolAccountInfo *block_account = &(params->ka[1]);
    SolAccountInfo *entry_account = &(params->ka[2]);
    SolAccountInfo *entry_token_owner_account = &(params->ka[3]);
    SolAccountInfo *entry_token_account = &(params->ka[4]);
    SolAccountInfo *stake_account = &(params->ka[5]);
    SolAccountInfo *ki_destination_account = &(params->ka[6]);
    SolAccountInfo *ki_destination_owner_account = &(params->ka[7]);
    SolAccountInfo *ki_mint_account = &(params->ka[8]);
    SolAccountInfo *authority_account = &(params->ka[9]);
    // The account at index 10 must be the system program, but this is not checked; if it's not that program, then
    // the transaction will simply fail when it is used to sign a cross-program invoke later
    // The account at index 11 must be the SPL token program, but this is not checked; if it's not that program, then
    // the transaction will simply fail when it is used to sign a cross-program invoke later
    // The account at index 12 must be the SPL associated token account program, but this is not checked; if it's not
    // that program, then the transaction will simply fail when it is used to sign a cross-program invoke later

    // Make sure that the input data is the correct size
    if (params->data_len != sizeof(HarvestData)) {
        return Error_InvalidDataSize;
    }
    
    // Cast to instruction data
    HarvestData *data = (HarvestData *) params->data;
    
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
    if (!ki_destination_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 6;
    }
    if (!ki_mint_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 8;
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
        return Error_NotStaked;
    }

    // Check to make sure that the entry token is owned by the token owner account
    if (!is_token_owner(entry_token_account, entry_token_owner_account->key, &(entry->mint_account.address))) {
        return Error_InvalidAccount_First + 4;
    }

    // Check to make sure that the stake account passed in is actually staked in the entry
    if (!SolPubkey_same(&(entry->staked.stake_account), stake_account->key)) {
        return Error_InvalidAccount_First + 5;
    }

    // Check to make sure that the proper Ki mint account was passed in
    if (!is_ki_mint_account(ki_mint_account->key)) {
        return Error_InvalidAccount_First + 8;
    }
    
    // Check to make sure that the proper nifty program authority account was passed in
    if (!is_nifty_authority_account(authority_account->key)) {
        return Error_InvalidAccount_First + 9;
    }
    
    // Decode the stake account
    Stake stake;
    if (!decode_stake_account(stake_account, &stake)) {
        return Error_InvalidAccount_First + 5;
    }

    // Harvest Ki.  Must be done before commission is charged since commission charge actually reduces the number of
    // lamports in the stake account, which would affect Ki harvest calculations
    return harvest_ki(&stake, entry, ki_destination_account, ki_destination_owner_account->key,
                      funding_account->key, params->ka, params->ka_num);
}
