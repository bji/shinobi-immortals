#pragma once

#include "util/util_commission.c"
#include "util/util_ki.c"
#include "util/util_stake.c"

typedef struct
{
    // This is the instruction code for Destake
    uint8_t instruction_code;

    // This is the new authority to set on the de-staked stake account
    SolPubkey new_withdraw_authority_pubkey;
    
} DestakeData;


static uint64_t user_destake(SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,   funding_account,                  ReadWrite,  Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(1,   block_account,                    ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,   entry_account,                    ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(3,   token_owner_account,              ReadOnly,   Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(4,   token_account,                    ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(5,   stake_account,                    ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(6,   ki_destination_account,           ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(7,   ki_destination_owner_account,     ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(8,   master_stake_account,             ReadWrite,  NotSigner,  KnownAccount_MasterStake);
        DECLARE_ACCOUNT(9,   bridge_stake_account,             ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(10,  ki_mint_account,                  ReadWrite,  NotSigner,  KnownAccount_KiMint);
        DECLARE_ACCOUNT(11,  authority_account,                ReadOnly,   NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(12,  clock_sysvar_account,             ReadOnly,   NotSigner,  KnownAccount_ClockSysvar);
        DECLARE_ACCOUNT(13,  system_program_account,           ReadOnly,   NotSigner,  KnownAccount_SystemProgram);
        DECLARE_ACCOUNT(14,  stake_program_account,            ReadOnly,   NotSigner,  KnownAccount_StakeProgram);
        DECLARE_ACCOUNT(15,  stake_history_sysvar_account,     ReadOnly,   NotSigner,  KnownAccount_StakeHistorySysvar);
        DECLARE_ACCOUNT(16,  spl_token_program_account,        ReadOnly,   NotSigner,  KnownAccount_SPLTokenProgram);
        DECLARE_ACCOUNT(17,  spl_ata_program_account,          ReadOnly,   NotSigner,  KnownAccount_SPLATAProgram);
    }
    DECLARE_ACCOUNTS_NUMBER(18);

    // Make sure that the input data is the correct size
    if (params->data_len != sizeof(DestakeData)) {
        return Error_InvalidDataSize;
    }
    
    // Cast to instruction data
    DestakeData *data = (DestakeData *) params->data;
    
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
    Entry *entry = get_validated_entry_of_block(entry_account, block_account->key);
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
    if (!is_token_owner(token_account, token_owner_account->key, &(entry->mint_pubkey), 1)) {
        return Error_InvalidAccount_First + 4;
    }

    // Check to make sure that the stake account passed in is actually staked in the entry
    if (!SolPubkey_same(&(entry->owned.stake_account), stake_account->key)) {
        return Error_InvalidAccount_First + 5;
    }

    // Decode the stake account
    Stake stake;
    if (!decode_stake_account(stake_account, &stake)) {
        return Error_InvalidAccount_First + 5;
    }

    // Harvest Ki.  Must be done before commission is charged since commission charge actually reduces the number of
    // lamports in the stake account, which would affect Ki harvest calculations
    uint64_t ret = harvest_ki(&stake, entry, ki_destination_account, ki_destination_owner_account->key,
                              funding_account->key, params->ka, params->ka_num);
    if (ret) {
        return ret;
    }

    // Charge commission
    ret = charge_commission(&stake, block, entry, funding_account->key, bridge_stake_account, stake_account->key,
                            params->ka, params->ka_num);
    if (ret) {
        return ret;
    }

    // Use stake account program to set all authorities to the new authority; must do this signed since the
    // nifty program authority is currently the withdraw authority of the stake account
    if (set_stake_authorities_signed(stake_account->key, &(data->new_withdraw_authority_pubkey),
                                     params->ka, params->ka_num)) {
        return Error_SetStakeAuthoritiesFailed;
    }

    // No longer staked, all fields in owned should be zeroed out
    sol_memset(&(entry->owned), 0, sizeof(entry->owned));
    
    return 0;
}
