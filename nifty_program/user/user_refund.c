#pragma once


static uint64_t user_refund(SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,   token_owner_account,              ReadOnly,   Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(1,   block_account,                    ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,   entry_account,                    ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(3,   authority_account,                ReadWrite,  NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(4,   token_account,                    ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(5,   destination_account,              ReadWrite,  NotSigner,  KnownAccount_NotKnown);
    }
    DECLARE_ACCOUNTS_NUMBER(6);

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
    Entry *entry = get_validated_entry_of_block(entry_account, block_account->key);
    if (!entry) {
        return Error_InvalidAccount_First + 2;
    }

    // Check to make sure that the entry token account is the owning token account of the single token of this
    // mint, and that the token_owner_account is the proper owner of that token account
    if (!is_token_owner(token_account, token_owner_account->key, &(entry->mint_pubkey), 1)) {
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

    // Check to make sure that the block's reveal grace period has passed
    if ((block->mystery_phase_end_timestamp + block->config.reveal_period_duration) > clock.unix_timestamp) {
        return Error_EntryWaitingForReveal;
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
