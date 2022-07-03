#pragma once

// If the user owns the entry, and if the entry is not revealed, and if the reveal grace period has passed,
// and if the entry has not been refunded yet, then return the price of the entry from the escrow (authority)
// account and mark the entry as refunded

// Account references:
// 0. `[SIGNER]` -- The entry's token account's owner
// 1. `[]` -- The block account
// 2. `[WRITE]` -- The entry account
// 3. `[WRITE]` -- The nifty program authority account
// 4. `[]` -- The entry token account
// 5. `[WRITE]` -- The destination account for the refunded lamports

typedef struct
{
    // This is the instruction code for Refund
    uint8_t instruction_code;

    // This is the entry_index of the entry within its block
    uint16_t entry_index;
    
} RefundData;


static uint64_t user_refund(SolParameters *params)
{
    // Sanitize the accounts.  There must be exactly 6.
    if (params->ka_num != 6) {
        return Error_IncorrectNumberOfAccounts;
    }

    SolAccountInfo *token_owner_account = &(params->ka[0]);
    SolAccountInfo *block_account = &(params->ka[1]);
    SolAccountInfo *entry_account = &(params->ka[2]);
    SolAccountInfo *authority_account = &(params->ka[3]);
    SolAccountInfo *token_account = &(params->ka[4]);
    SolAccountInfo *destination_account = &(params->ka[5]);

    // Make sure that the input data is the correct size
    if (params->data_len != sizeof(RefundData)) {
        return Error_InvalidDataSize;
    }

    // Cast to instruction data
    RefundData *data = (RefundData *) params->data;
    
    // Check permissions
    if (!token_owner_account->is_signer) {
        return Error_InvalidAccountPermissions_First;
    }
    if (!entry_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 2;
    }
    if (!authority_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 3;
    }
    if (!destination_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 5;
    }

    // Get validated block account
    Block *block = get_validated_block(block_account);
    if (!block) {
        return Error_InvalidAccount_First + 1;
    }

    // Ensure that the block is complete; cannot refund from a block that is not complete yet
    if (!is_block_complete(block)) {
        return Error_BlockNotComplete;
    }

    // Get validated entry account
    Entry *entry = get_validated_entry(block, data->entry_index, entry_account);
    if (!entry) {
        return Error_InvalidAccount_First + 2;
    }

    // Check to make sure that the authority account provided is the correct account
    if (!is_nifty_authority_account(authority_account->key)) {
        return Error_InvalidAccount_First + 3;
    }

    // Check to make sure that the entry token account is the owning token account of the single token of this
    // mint, and that the token_owner_account is the proper owner of that token account
    if (!is_token_owner(token_owner_account, token_account, &(entry->mint_account.address))) {
        return Error_InvalidAccount_First + 4;
    }
    
    // Need the clock now
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }

    // Check to make sure that the entry is waiting to be revealed and is owned
    if (get_entry_state(block, entry, &clock) != EntryState_WaitingForRevealOwned) {
        return Error_EntryNotRevealable;
    }
    
    // Check to make sure that the entry was not already refunded
    if (entry->refund_awarded) {
        return Error_AlreadyRefunded;
    }

    // OK, issue the refund, by taking lamports from the authority account and putting them in the destination account
    *(authority_account->lamports) -= entry->purchase_price_lamports;
    *(destination_account->lamports) += entry->purchase_price_lamports;

    // And mark it as refunded
    entry->refund_awarded = true;

    return 0;
}
