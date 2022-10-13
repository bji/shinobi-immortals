#pragma once


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

    // Compute how much Ki to burn from the source account
    uint64_t ki_to_burn = entry->metadata.level_1_ki;
    for (uint8_t i = 0; i < entry->level; i++) {
        // Multiply by 1.5x
        ki_to_burn += (ki_to_burn >> 1);
    }

    // Multiply the number of ki to burn by 10 because on-chain Ki are stored with decimal places 1, or in other
    // words, as "DeciKi"
    ki_to_burn *= 10;

    // Check to make sure that the ki source account passed in is really a ki account owned by the passed in owner
    // and has the required amount of Ki
    if (!is_token_owner(ki_source_account, ki_source_owner_account->key, &(Constants.ki_mint_pubkey), ki_to_burn)) {
        return Error_InvalidAccount_First + 5;
    }

    // Burn the Ki
    uint64_t ret = burn_tokens(ki_source_account->key, ki_source_owner_account->key,
                               &(Constants.ki_mint_pubkey), ki_to_burn, params->ka, params->ka_num);
    if (ret) {
        return ret;
    }

    // Increase the entry's level
    entry->level += 1;

    // Update the metaplex metadata
    return set_metaplex_metadata_for_level(entry, entry->level, entry_metadata_account, params->ka, params->ka_num);
}
