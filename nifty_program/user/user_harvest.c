#pragma once


static uint64_t user_harvest(SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,   funding_account,                  ReadWrite,  Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(1,   entry_account,                    ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,   token_owner_account,              ReadOnly,   Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(3,   token_account,                    ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(4,   stake_account,                    ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(5,   ki_destination_account,           ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(6,   ki_destination_owner_account,     ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(7,   ki_mint_account,                  ReadWrite,  NotSigner,  KnownAccount_KiMint);
        DECLARE_ACCOUNT(8,   authority_account,                ReadOnly,   NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(9,   system_program_account,           ReadOnly,   NotSigner,  KnownAccount_SystemProgram);
        DECLARE_ACCOUNT(10,  spl_token_program_account,        ReadOnly,   NotSigner,  KnownAccount_SPLTokenProgram);
        DECLARE_ACCOUNT(11,  spl_ata_program_account,          ReadOnly,   NotSigner,  KnownAccount_SPLATAProgram);
    }
    
    DECLARE_ACCOUNTS_NUMBER(12);

    // This is the entry data
    Entry *entry = get_validated_entry(entry_account);
    if (!entry) {
        return Error_InvalidAccount_First + 1;
    }

    // Get the clock sysvar, needed below
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }

    // Check to make sure that the entry is staked
    if (get_entry_state(0, entry, &clock) != EntryState_OwnedAndStaked) {
        return Error_NotStaked;
    }

    // Check to make sure that the entry token is owned by the token owner account
    if (!is_token_owner(token_account, token_owner_account->key, &(entry->mint_pubkey), 1)) {
        return Error_InvalidAccount_First + 3;
    }

    // Check to make sure that the stake account passed in is actually staked in the entry
    if (!SolPubkey_same(&(entry->owned.stake_account), stake_account->key)) {
        return Error_InvalidAccount_First + 4;
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
