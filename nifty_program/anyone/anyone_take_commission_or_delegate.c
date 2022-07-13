#pragma once

// Account references:
// 0. `[WRITE, SIGNER]` -- The account to use for funding operations
// 1. `[]` -- The block account address
// 2. `[WRITE]` -- The entry account address
// 3. `[WRITE]` -- The stake account
// 4. `[WRITE]` -- The master stake account
// 5. `[WRITE]` -- The bridge stake account
// 6. `[]` -- The nifty authority account (for commission charge splitting/merging)
// 7. `[]` -- Clock sysvar (required by stake program)
// 8. `[]` -- The system program id (for cross-program invoke)
// 9. `[]` -- The stake program id (for cross-program invoke)
// 10. `[]` -- The stake history id

typedef struct
{
    // This is the instruction code for TakeCommission
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
    
} TakeCommissionData;


static void sol_log_paramsy(const SolParameters *params) {
  sol_log("- Program identifier:");
  sol_log_pubkey(params->program_id);

  sol_log("- Number of KeyedAccounts");
  sol_log_64(0, 0, 0, 0, params->ka_num);
  for (int i = 0; i < params->ka_num; i++) {
    sol_log("  - Key");
    sol_log_pubkey(params->ka[i].key);
    sol_log("  - Is signer");
    sol_log_64(0, 0, 0, 0, params->ka[i].is_signer);
    sol_log("  - Is writable");
    sol_log_64(0, 0, 0, 0, params->ka[i].is_writable);
    sol_log("  - Lamports");
    sol_log_64(0, 0, 0, 0, *params->ka[i].lamports);
    sol_log("  - Owner");
    sol_log_pubkey(params->ka[i].owner);
    sol_log("  - Executable");
    sol_log_64(0, 0, 0, 0, params->ka[i].executable);
    sol_log("  - Rent Epoch");
    sol_log_64(0, 0, 0, 0, params->ka[i].rent_epoch);
  }
}


static uint64_t anyone_take_commission_or_delegate(SolParameters *params)
{
    // Sanitize the accounts.  There must be exactly 11.
    if (params->ka_num != 11) {
        return Error_IncorrectNumberOfAccounts;
    }

    SolAccountInfo *funding_account = &(params->ka[0]);
    SolAccountInfo *block_account = &(params->ka[1]);
    SolAccountInfo *entry_account = &(params->ka[2]);
    SolAccountInfo *stake_account = &(params->ka[3]);
    SolAccountInfo *master_stake_account = &(params->ka[4]);
    SolAccountInfo *bridge_stake_account = &(params->ka[5]);
    SolAccountInfo *authority_account = &(params->ka[6]);
    SolAccountInfo *clock_sysvar_account = &(params->ka[7]);
    // The account at index 8 must be the system program, but this is not checked; if it's not that program, then
    // the transaction will simply fail when it is used to sign a cross-program invoke later
    // The account at index 9 must be the SPL token program, but this is not checked; if it's not that program, then
    // the transaction will simply fail when it is used to sign a cross-program invoke later
    // The account at index 10 must be the SPL associated token account program, but this is not checked; if it's not
    // that program, then the transaction will simply fail when it is used to sign a cross-program invoke later

    // Make sure that the input data is the correct size
    if (params->data_len != sizeof(TakeCommissionData)) {
        return Error_InvalidDataSize;
    }
    
    // Cast to instruction data
    TakeCommissionData *data = (TakeCommissionData *) params->data;
    
    // Check permissions
    if (!funding_account->is_writable) {
        return Error_InvalidAccountPermissions_First;
    }
    if (!entry_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 2;
    }
    if (!stake_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 3;
    }
    if (!master_stake_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 4;
    }
    if (!bridge_stake_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 5;
    }

    // Get validated block and entry, which checks all validity of those accounts
    Block *block = get_validated_block(block_account);
    if (!block) {
        return Error_InvalidAccount_First + 1;
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

    // Check to make sure that the stake account passed in is actually staked in the entry
    if (!SolPubkey_same(&(entry->staked.stake_account), stake_account->key)) {
        return Error_InvalidAccount_First + 3;
    }

    // Check to make sure that the master stake account passed in is actually the master stake account
    if (!is_master_stake_account(master_stake_account->key)) {
        return Error_InvalidAccount_First + 4;
    }
    
    // Check to make sure that the proper bridge account was passed in
    SolPubkey bridge_pubkey;
    if (!compute_bridge_address(block->config.group_number, block->config.block_number, entry->entry_index,
                                data->bridge_bump_seed, &bridge_pubkey) ||
        !SolPubkey_same(&bridge_pubkey, bridge_stake_account->key)) {
        return Error_InvalidAccount_First + 5;
    }
    
    // Check to make sure that the proper nifty program authority account was passed in
    if (!is_nifty_authority_account(authority_account->key)) {
        return Error_InvalidAccount_First + 6;
    }
    
    // Check to make sure that the proper clock sysvar account was passed in
    if (!SolPubkey_same(&(Constants.clock_sysvar_pubkey), clock_sysvar_account->key)) {
        return Error_InvalidAccount_First + 7;
    }
    
    // Decode the stake account
    Stake stake;
    if (!decode_stake_account(stake_account, &stake)) {
        return Error_InvalidAccount_First + 3;
    }

    // If the stake account is in an initialized state, then it's not delegated, so delegate it to Shinobi Systems
    if (stake.state == StakeState_Initialized) {
        return delegate_stake_signed(stake_account->key, &(Constants.shinobi_systems_vote_pubkey),
                                     params->ka, params->ka_num);
    }
    // Else, it's initialized, so try charging commission
    else {
        return charge_commission(&stake, block, entry, funding_account->key, bridge_stake_account,
                                 data->bridge_bump_seed, stake_account->key, data->minimum_stake_lamports,
                                 params->ka, params->ka_num);
    }
}
