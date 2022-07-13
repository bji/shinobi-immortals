#pragma once

#include "util/util_stake.c"

// Account references:
// 0. `[]` -- The block account address
// 1. `[WRITE]` -- The entry account address
// 2. `[SIGNER]` -- The token owner account
// 3. `[]` -- The token account (holding the entry's token)
// 4. `[WRITE]` -- The stake account
// 5. `[SIGNER]` -- The stake account withdraw authority
// 6. `[]` -- The Shinobi Systems vote account
// 7. `[]` -- The nifty authority account
// 8. `[]` -- Clock sysvar (required by stake program)
// 9. `[]` -- The stake program id (for cross-program invoke)
// 10. `[]` -- The stake config account (for staking purposes)
// 11. `[]` -- Stake history sysvar (required by stake program)

typedef struct
{
    // This is the instruction code for Buy
    uint8_t instruction_code;

    // This is the entry_index of the entry within its block
    uint16_t entry_index;
    
} StakeData;


static uint64_t user_stake(SolParameters *params)
{
    // Sanitize the accounts.  There must be exactly 12.
    if (params->ka_num != 12) {
        return Error_IncorrectNumberOfAccounts;
    }

    SolAccountInfo *block_account = &(params->ka[0]);
    SolAccountInfo *entry_account = &(params->ka[1]);
    SolAccountInfo *token_owner_account = &(params->ka[2]);
    SolAccountInfo *token_account = &(params->ka[3]);
    SolAccountInfo *stake_account = &(params->ka[4]);
    SolAccountInfo *stake_withdraw_authority_account = &(params->ka[5]);
    SolAccountInfo *shinobi_systems_vote_account = &(params->ka[6]);
    SolAccountInfo *authority_account = &(params->ka[7]);
    SolAccountInfo *clock_sysvar_account = &(params->ka[8]);
    // The account at index 9 must be the stake program, but this is not checked; if it's not that program, then
    // the transaction will simply fail when it is used to sign a cross-program invoke later
    // The account at index 10 must be the stake config account, but this is not checked; if it's not that account,
    // then the transaction will simply fail when it is used to do a stake delegate later
    // The account at index 11 must be the stake history sysvar account, but this is not checked; if it's not that
    // account, then the transaction will simply fail when it is used to do a stake delegate later

    // Make sure that the input data is the correct size
    if (params->data_len != sizeof(StakeData)) {
        return Error_InvalidDataSize;
    }

    // Cast to instruction data
    StakeData *data = (StakeData *) params->data;

    // Check permissions
    if (!entry_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 1;
    }
    if (!token_owner_account->is_signer) {
        return Error_InvalidAccountPermissions_First + 2;
    }
    if (!stake_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 3;
    }
    if (!stake_withdraw_authority_account->is_signer) {
        return Error_InvalidAccountPermissions_First + 4;
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
        return Error_InvalidAccount_First + 1;
    }
    
    // Check to make sure that the clock sysvar account really is a reference to that account
    if (!SolPubkey_same(&(Constants.clock_sysvar_pubkey), clock_sysvar_account->key)) {
        return Error_InvalidAccount_First + 7;
    }

    // Get the clock sysvar, needed below
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }

    // Check to make sure that the entry is in an Owned state, which is the only state from which a stake operation
    // is valid.
    if (get_entry_state(block, entry, &clock) != EntryState_Owned) {
        return Error_NotStakeable;
    }

    // Deserialize the stake account into a Stake instance
    Stake stake;
    if (!decode_stake_account(stake_account, &stake)) {
        return Error_InvalidAccount_First + 2;
    }
    
    // - Must be in Initialized or Stake state
    switch (stake.state) {
    case StakeState_Initialized:
    case StakeState_Stake:
        break;
    default:
        return Error_InvalidAccount_First + 2;
    }
    
    // - Must have a withdraw authority equal to the provided withdraw authority
    if (!SolPubkey_same(&(stake.meta.authorize.withdrawer), stake_withdraw_authority_account->key)) {
        return Error_InvalidAccount_First + 2;
    }

    // - Must not be locked.  Don't bother checking custodian, that feature just isn't supported here
    if ((stake.meta.lockup.unix_timestamp > clock.unix_timestamp) || (stake.meta.lockup.epoch > clock.epoch)) {
        return Error_InvalidAccount_First + 2;
    }

    // Check to make sure that the entry token is owned by the token owner account
    if (!is_token_owner(token_account, token_owner_account->key, &(entry->mint_account.address), 1)) {
        return Error_InvalidAccount_First + 3;
    }

    // Make sure that the supplied Shinobi Systems vote account is the correct account
    if (!is_shinobi_systems_vote_account(shinobi_systems_vote_account->key)) {
        return Error_InvalidAccount_First + 6;
    }

    // Make sure that the supplied authority account is the correct account
    if (!is_nifty_authority_account(authority_account->key)) {
        return Error_InvalidAccount_First + 7;
    }

    // Use stake account program to set all authorities to the nifty authority
    if (!set_stake_authorities(stake_account->key, stake_withdraw_authority_account->key,
                               &(Constants.nifty_authority_pubkey), params->ka, params->ka_num)) {
        return Error_SetStakeAuthoritiesFailed;
    }

    // If the stake account is in an initialized state, then it's not delegated, so delegate it to Shinobi Systems
    if (stake.state == StakeState_Initialized) {
        if (!delegate_stake_signed(stake_account->key, &(Constants.shinobi_systems_vote_pubkey),
                                   params->ka, params->ka_num)) {
            return Error_FailedToDelegate;
        }
    }
    // Else the stake account is delegated (because the only other stake state possible is StakeState_Stake according
    // to the switch done already above), and if it's not delegated to Shinobi Systems, deactivate it, so that in the
    // next epoch it can be re-delegated to Shinobi Systems via the redelegate crank.
    else if (!is_shinobi_systems_vote_account(&(stake.stake.delegation.voter_pubkey))) {
        if (!deactivate_stake_signed(stake_account->key, params->ka, params->ka_num)) {
            return Error_FailedToDeactivate;
        }
    }
    
    // Record the stake account address
    entry->staked.stake_account = *(stake_account->key);
      
    // Record current lamports in the stake account to be used for ki harvesting purposes
    entry->staked.last_ki_harvest_stake_account_lamports = stake.stake.delegation.stake;
        
    // Record current lamports in the stake account to be used for commission purposes 
    entry->staked.last_commission_charge_stake_account_lamports = stake.stake.delegation.stake;
    
    return 0;
}
