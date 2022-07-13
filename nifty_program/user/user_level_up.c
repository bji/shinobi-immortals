#pragma once


// If the user is the owner of the token then can present a ki token account that they own that has at least
// as many Ki tokens in it as required to go to the next level, if so the ki are burned and the entry is
// moved up a level and its metadata set.

// Account references:
// 0. `[]` -- The block account address
// 1. `[WRITE]` -- The entry account address
// 2. `[SIGNER]` -- The token owner account
// 3. `[]` -- The token account (holding the entry's token)
// 4. `[WRITE]` -- The entry metaplex metadata account
// 5. `[WRITE]` -- The source account for Ki
// 6. `[SIGNER]` -- The account that owns the Ki token account
// 7. `[WRITE]` -- The Ki mint account
// 8. `[]` -- The nifty authority account
// 9. `[]` -- The SPL Token account program
// 10. `[]` -- The metaplex metadata program

typedef struct
{
    // This is the instruction code for LevelUp
    uint8_t instruction_code;

    // This is the entry_index of the entry within its block
    uint16_t entry_index;
    
} LevelUpData;


static uint64_t user_level_up(SolParameters *params)
{
    // Sanitize the accounts.  There must be exactly 11.
    if (params->ka_num != 11) {
        return Error_IncorrectNumberOfAccounts;
    }

    SolAccountInfo *block_account = &(params->ka[0]);
    SolAccountInfo *entry_account = &(params->ka[1]);
    SolAccountInfo *token_owner_account = &(params->ka[2]);
    SolAccountInfo *token_account = &(params->ka[3]);
    SolAccountInfo *entry_metadata_account = &(params->ka[4]);
    SolAccountInfo *ki_source_account = &(params->ka[5]);
    SolAccountInfo *ki_source_owner_account = &(params->ka[6]);
    SolAccountInfo *ki_mint_account = &(params->ka[7]);
    SolAccountInfo *authority_account = &(params->ka[8]);
    // The account at index 9 must be the SPL token program, but this is not checked; if it's not that program, then
    // the transaction will simply fail when it is used to sign a cross-program invoke later
    // The account at index 10 must be the SPL associated token account program, but this is not checked; if it's not
    // that program, then the transaction will simply fail when it is used to sign a cross-program invoke later

    // Make sure that the input data is the correct size
    if (params->data_len != sizeof(LevelUpData)) {
        return Error_InvalidDataSize;
    }
    
    // Cast to instruction data
    LevelUpData *data = (LevelUpData *) params->data;
    
    // Check permissions
    if (!entry_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 2;
    }
    if (!token_owner_account->is_signer) {
        return Error_InvalidAccountPermissions_First + 3;
    }
    if (!entry_metadata_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 4;
    }
    if (!ki_source_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 5;
    }
    if (!ki_source_owner_account->is_signer) {
        return Error_InvalidAccountPermissions_First + 6;
    }
    if (!ki_mint_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 7;
    }

    // Get validated block and entry, which checks all validity of those accounts
    Block *block = get_validated_block(block_account);
    if (!block) {
        return Error_InvalidAccount_First;
    }

    // Ensure that the block is complete; cannot level up in a block that is not complete yet
    if (!is_block_complete(block)) {
        return Error_BlockNotComplete;
    }

    // This is the entry data
    Entry *entry = get_validated_entry(block, data->entry_index, entry_account);
    if (!entry) {
        return Error_InvalidAccount_First + 1;
    }

    // Get the clock sysvar, needed below
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }

    // Check to make sure that the entry is Owned or OwnedAndStaked
    switch (get_entry_state(block, entry, &clock)) {
    case EntryState_Owned:
    case EntryState_OwnedAndStaked:
        break;
    default:
        return Error_NotOwned;
    }

    // Check to make sure that the entry is not already at level index 8 (which is known to the user as level 9)
    if (entry->metadata.level == 8) {
        return Error_AlreadyAtMaxLevel;
    }

    // Check to make sure that the entry token is owned by the token owner account
    if (!is_token_owner(token_account, token_owner_account->key, &(entry->mint_account.address), 1)) {
        return Error_InvalidAccount_First + 3;
    }
    // Check to make sure that the correct entry metadata account was passed in
    if (!SolPubkey_same(&(entry->metaplex_metadata_account), entry_metadata_account->key)) {
        return Error_InvalidAccount_First + 4;
    }

    // Compute how much Ki to burn from the source account
    uint64_t ki_to_burn = entry->metadata.level_1_ki;
    for (uint8_t i = 0; i < entry->metadata.level; i++) {
        // Multiply by 1.5x
        ki_to_burn += (ki_to_burn >> 1);
    }

    // Check to make sure that the ki source account passed in is really a ki account owned by the passed in owner
    // and has the required amount of Ki
    if (!is_token_owner(ki_source_account, ki_source_owner_account->key, &(Constants.ki_mint_pubkey), ki_to_burn)) {
        return Error_InvalidAccount_First + 5;
    }

    // Check to make sure that the proper ki mint account was passed in
    if (!is_ki_mint_account(ki_mint_account->key)) {
        return Error_InvalidAccount_First + 7;
    }

    // Burn the Ki
    uint64_t ret = burn_tokens(ki_source_account->key, ki_source_owner_account->key,
                               &(Constants.ki_mint_pubkey), ki_to_burn, params->ka, params->ka_num);
    if (ret) {
        return ret;
    }

    // Increase the entry's level
    entry->metadata.level += 1;

    // Update the metaplex metadata
    return set_metaplex_metadata_for_level(entry, entry->metadata.level, entry_metadata_account,
                                           params->ka, params->ka_num);
}
