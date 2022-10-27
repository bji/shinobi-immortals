#pragma once

typedef struct
{
    // This is the instruction code for Reauthorize
    uint8_t instruction_code;

    // This is the new authority that will be given stake and withdraw authority for any stake account staked
    // to the entry, and the enty's metadata.
    SolPubkey new_authority;

} ReauthorizeData;


static uint64_t special_reauthorize(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,   config_account,                ReadOnly,   NotSigner,   KnownAccount_ProgramConfig);
        DECLARE_ACCOUNT(1,   admin_account,                 ReadOnly,   Signer,      KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,   entry_account,                 ReadWrite,  NotSigner,   KnownAccount_NotKnown);
        DECLARE_ACCOUNT(3,   token_owner_account,           ReadOnly,   Signer,      KnownAccount_NotKnown);
        DECLARE_ACCOUNT(4,   token_account,                 ReadOnly,   NotSigner,   KnownAccount_NotKnown);
        DECLARE_ACCOUNT(5,   entry_metadata_account,        ReadWrite,  NotSigner,   KnownAccount_NotKnown);
        DECLARE_ACCOUNT(6,   stake_account,                 ReadWrite,  NotSigner,   KnownAccount_NotKnown);
        DECLARE_ACCOUNT(7,   authority_account,             ReadOnly,   NotSigner,   KnownAccount_Authority);
        DECLARE_ACCOUNT(8,   clock_sysvar_account,          ReadOnly,   NotSigner,   KnownAccount_ClockSysvar);
        DECLARE_ACCOUNT(9,   metaplex_program_account,      ReadOnly,   NotSigner,   KnownAccount_MetaplexProgram);
        DECLARE_ACCOUNT(10,  stake_program_account,         ReadOnly,   NotSigner,   KnownAccount_StakeProgram);
    }
    DECLARE_ACCOUNTS_NUMBER(11);

    // Ensure that the transaction was authorized by the admin
    if (!is_admin_account(config_account, admin_account->key)) {
        return Error_PermissionDenied;
    }

    // Ensure that the data is of the correct size
    if (params->data_len != sizeof(ReauthorizeData)) {
        return Error_InvalidDataSize;
    }

    // Can safely use the data now
    const ReauthorizeData *data = (ReauthorizeData *) params->data;

    // This is the entry data
    Entry *entry = get_validated_entry(entry_account);
    if (!entry) {
        return Error_InvalidAccount_First + 2;
    }

    // Get the clock sysvar, needed below
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }

    // Make sure that the entry is in an Owned or OwnedAndStaked state
    bool is_staked;
    switch (get_entry_state(0, entry, &clock)) {
    case EntryState_Owned:
        is_staked = false;
        break;
    case EntryState_OwnedAndStaked:
        is_staked = true;
        break;
    default:
        return Error_InvalidAccount_First + 2;
    }

    // Check to make sure that the entry token is owned by the token owner account
    if (!is_token_owner(token_account, token_owner_account->key, &(entry->mint_pubkey), 1)) {
        return Error_InvalidAccount_First + 3;
    }

    // Check to make sure that the correct metaplex metadata account was provided
    if (!SolPubkey_same(entry_metadata_account->key, &(entry->metaplex_metadata_pubkey))) {
        return Error_InvalidAccount_First + 6;
    }

    // Set the metaplex metadata authority of the entry to the new authority
    uint64_t ret = set_metaplex_metadata_authority(&(entry->metaplex_metadata_pubkey), &(data->new_authority),
                                                   params->ka, params->ka_num);
    if (ret) {
        return ret;
    }

    // If the entry is staked, then set new authorities on it too
    if (is_staked) {
        // Make sure that the passed-in stake account is the correct one
        if (!SolPubkey_same(stake_account->key, &(entry->owned.stake_account))) {
            return Error_InvalidAccount_First + 5;
        }

        // And ensure that it's read-write; couldn't enforce this in the declared accounts list, because the system
        // program (all zeroes) is expected if there is no stake account to modify, and that will be forced to
        // read-only by the runtime regardless of the transaction settings on the account
        if (!stake_account->is_writable) {
            return Error_InvalidAccountPermissions_First + 6;
        }

        uint64_t ret = set_stake_authorities_signed(&(entry->owned.stake_account), &(data->new_authority),
                                                    params->ka, params->ka_num);
        if (ret) {
            return ret;
        }

        // No longer staked, all fields in owned should be zeroed out
        sol_memset(&(entry->owned), 0, sizeof(entry->owned));
    }

    return 0;
}
