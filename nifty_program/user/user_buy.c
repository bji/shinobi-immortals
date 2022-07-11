#pragma once

#include "inc/types.h"
#include "util/util_token.c"
#include "util/util_transfer_lamports.c"


// Account references:
// 0. `[WRITE, SIGNER]` -- The funding account to pay all fees associated with this transaction
// 1. `[]` -- Program config account
// 2. `[WRITE]` -- The admin account
// 3. `[WRITE iff buying mystery]` -- The nifty authority account
// 4. `[WRITE iff buying mystery]` -- The block account address
// 5. `[WRITE]` -- The entry account address
// 6. `[WRITE]` -- The entry token account (must be a PDA of the nifty program, derived from ticket_token_seed from
//                 instruction data)
// 7. `[]` -- The entry mint account
// 8. `[WRITE]` -- The account to give ownership of the entry (token) to.  It will be created if it doesn't exist.
// 9. `[]` -- The system account that will own the destination token account if it needs to be created; or currently
//            owns it if it's already created
// 10. `[WRITE]` -- The metaplex metadata account for the entry
// 11. `[]` -- The nifty program, for cross-program invoke signature
// 12. `[]` -- The SPL token program, for cross-program invoke
// 13. `[]` -- The SPL associated token account program, for cross-program invoke
// 14. `[]` -- The metaplex metadata program, for cross-program invoke
// 15. `[]` -- The system program id, for cross-program invoke

typedef struct
{
    // This is the instruction code for Buy
    uint8_t instruction_code;

    // This is the entry_index of the entry within its block
    uint16_t entry_index;
    
} BuyData;


// If start_price > 100,000 SOL, rounding errors could be significant.
static uint64_t compute_price(uint64_t total_seconds, uint64_t start_price, uint64_t end_price,
                              uint64_t seconds_elapsed)
{
    // Once the elapsed seconds hits the total seconds, the result is always end_price 
    if (seconds_elapsed >= total_seconds) {
        return end_price;
    }

    uint64_t delta = start_price - end_price;

    // This is a curve based on the formula: y = (1 / (100x + 1)) - (1 / 101)
    // To avoid rounding errors in math, work with lamports / 1000
    delta /= 1000ull;
    end_price /= 1000ull;

    uint64_t ac = delta * 101ull;

    uint64_t ab = ((100ull * delta * seconds_elapsed) / total_seconds) + delta;

    uint64_t bc = ((100ull * 101ull * seconds_elapsed) / total_seconds) + 101ull;

    return (end_price + ((ac - ab) / bc)) * 1000ull;
}


static uint64_t user_buy(SolParameters *params)
{
    // Sanitize the accounts.  There must be exactly 16.
    if (params->ka_num != 16) {
        return Error_IncorrectNumberOfAccounts;
    }

    SolAccountInfo *funding_account = &(params->ka[0]);
    SolAccountInfo *config_account = &(params->ka[1]);
    SolAccountInfo *admin_account = &(params->ka[2]);
    SolAccountInfo *authority_account = &(params->ka[3]);
    SolAccountInfo *block_account = &(params->ka[4]);
    SolAccountInfo *entry_account = &(params->ka[5]);
    SolAccountInfo *entry_token_account = &(params->ka[6]);
    SolAccountInfo *entry_mint_account = &(params->ka[7]);
    SolAccountInfo *token_destination_account = &(params->ka[8]);
    SolAccountInfo *token_destination_account_owner = &(params->ka[9]);
    SolAccountInfo *metaplex_metadata_account = &(params->ka[10]);
    // The account at index 11 must be the nifty program, but this is not checked; if it's not that program, then
    // the transaction will simply fail when it is used to sign a cross-program invoke later
    // The account at index 12 must be the SPL token program, but this is not checked; if it's not that program, then
    // the transaction will simply fail when it is invoked later
    // The account at index 13 must be the SPL associated token account program, but this is not checked; if it's not
    // that program, then the transaction will simply fail when it is invoked later
    // The account at index 14 must be the metaplex metadata program, but this is not checked; if it's not that
    // program, then the transaction will simply fail when the metadata is updated
    // The account at index 15 must be the system program, but this is not checked; if it's not that program, then
    // the transaction will simply fail when the lamports transfer happens later

    // Make sure that the input data is the correct size
    if (params->data_len != sizeof(BuyData)) {
        return Error_InvalidDataSize;
    }

    // Cast to instruction data
    BuyData *data = (BuyData *) params->data;

    // Check permissions
    if (!funding_account->is_writable || !funding_account->is_signer) {
        return Error_InvalidAccountPermissions_First;
    }
    if (!admin_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 2;
    }
    if (!entry_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 2;
    }
    if (!entry_token_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 3;
    }
    if (!token_destination_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 5;
    }

    // Make sure that the suppled admin account is the correct account
    if (!is_admin_account(config_account, admin_account->key)) {
        return Error_InvalidAccount_First + 2;
    }

    // Make sure that the suppled authority account is the correct account
    if (!is_nifty_authority_account(authority_account->key)) {
        return Error_InvalidAccount_First + 3;
    }

    // This is the block data
    Block *block = get_validated_block(block_account);
    if (!block) {
        return Error_InvalidAccount_First + 4;
    }

    // Ensure that the block is complete; cannot buy anything from a block that is not complete yet
    if (!is_block_complete(block)) {
        return Error_BlockNotComplete;
    }

    // This is the entry data
    Entry *entry = get_validated_entry(block, data->entry_index, entry_account);
    if (!entry) {
        return Error_InvalidAccount_First + 5;
    }

    // Check that the correct token account address is supplied
    if (!SolPubkey_same(entry_token_account->key, &(entry->token_account.address))) {
        return Error_InvalidAccount_First + 6;
    }

    // Check that the correct mint account address is supplied
    if (!SolPubkey_same(entry_mint_account->key, &(entry->mint_account.address))) {
        return Error_InvalidAccount_First + 7;
    }
    
    // Check that the correct metaplex metadata account is supplied
    if (!SolPubkey_same(metaplex_metadata_account->key, &(entry->metaplex_metadata_account))) {
        return Error_InvalidAccount_First + 9;
    }

    // Get the clock sysvar, needed below
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }

    // No need to check that the token is owned by the nifty stakes program; if it is not, then the SPL token program
    // invoke of the transfer of the token from the token account to the destination account will simply fail and the
    // transaction will then fail

    SolAccountInfo *funds_destination_account;
    uint64_t purchase_price_lamports;

    switch (get_entry_state(block, entry, &clock)) {
    case EntryState_PreRevealOwned:
    case EntryState_WaitingForRevealOwned:
    case EntryState_Owned:
    case EntryState_OwnedAndStaked:
        // Already owned, can't be purchased again
        return Error_AlreadyPurchased;

    case EntryState_WaitingForRevealUnowned:
        // In reveal grace period waiting for reveal, can't be purchased
        return Error_EntryWaitingForReveal;

    case EntryState_InNormalAuction:
        // In normal auction, can't be purchased, can only be bid on
        return Error_EntryInAuction;

    case EntryState_WaitingToBeClaimed:
        // Has a winning auction bid, can't be purchased
        return Error_EntryWaitingToBeClaimed;
        
    case EntryState_PreRevealUnowned:
        // Pre-reveal but not owned yet.  Can be purchased as a mystery.

        // The authority account must be writable so that the purchase price lamports can be added to it
        if (!authority_account->is_writable) {
            return Error_InvalidAccountPermissions_First + 3;
        }

        // The block account must be writable so that the mysteries sold counter can be incremented
        if (!block_account->is_writable) {
            return Error_InvalidAccountPermissions_First + 4;
        }

        // The destination of funds is the authority account, which is where the purchase price is stored until
        // the entry is revealed (and if the entry is never revealed and the user requests a refund, then the
        // funds are removed from that account and returned back to the user)
        funds_destination_account = authority_account;
        
        // Compute price of mystery
        purchase_price_lamports = compute_price(block->config.mystery_phase_duration,
                                                block->config.mystery_start_price_lamports,
                                                block->config.minimum_price_lamports,
                                                clock.unix_timestamp - block->state.block_start_timestamp);

        // Update the entry's block to indicate that one more mystery was purchased.  If this is the last
        // mystery to purchase before the block becomes revealable, then the block reveal period begins.
        block->state.mysteries_sold_count += 1;
        if (block->state.mysteries_sold_count == block->config.total_mystery_count) {
            block->state.mysteries_all_sold_timestamp = clock.unix_timestamp;
        }

        break;

    case EntryState_Unowned:
        // Unowned, revealed, and not in a "normal" auction.  Can be purchased.

        // The write state of the nifty authority and block accounts indicates whether the user intends to buy a
        // mystery or a non-mystery.  Fail the buy if their desired mystery state doesn't match the state of the
        // entry, to prevent a user with stale information from buying what they think is a mystery when it no longer
        // is.
        if (authority_account->is_writable) {
            return Error_InvalidAccountPermissions_First + 3;
        }
        if (block_account->is_writable) {
            return Error_InvalidAccountPermissions_First + 4;
        }
        
        // The destination of funds is the admin account
        funds_destination_account = admin_account;

        // Compute purchase price.
        if (block->config.auction_duration) {
            // There was an auction; because this entry state is unowned, it was either a normal auction that ended
            // already (in which case compute_price will just return block->config.minimum_price_lamports), or it was
            // a reverse auction, in which case compute_price will just always do the right thing.
            purchase_price_lamports = compute_price(block->config.auction_duration,
                                                    block->config.reverse_auction_start_price_lamports,
                                                    block->config.minimum_price_lamports,
                                                    clock.unix_timestamp - entry->auction.begin_timestamp);
        }
        // If there was no auction, then the purchase price is just the minimum price
        else {
            purchase_price_lamports = block->config.minimum_price_lamports;
        }
        
        break;
    }

    // Check to ensure that the funds source has at least the purchase price lamports
    if (*(funding_account->lamports) < purchase_price_lamports) {
        return Error_InsufficientFunds;
    }

    // Transfer the purchase price from the funds source to the funds destination account
    uint64_t ret = util_transfer_lamports(funding_account->key, funds_destination_account->key, purchase_price_lamports,
                                          params->ka, params->ka_num);
    if (ret) {
        return ret;
    }

    // Ensure that the token destination account exists
    ret = create_token_account_idempotent(token_destination_account, token_destination_account_owner->key,
                                          &(entry->mint_account.address), funding_account->key, params->ka,
                                          params->ka_num);
    if (ret) {
        return ret;
    }
                                          
    // Transfer the token to the token destination account
    ret = transfer_entry_token(block, entry, token_destination_account, params->ka, params->ka_num);
    if (ret) {
        return ret;
    }

    // Set the purchase price in the Entry now that it's been purchased
    entry->purchase_price_lamports = purchase_price_lamports;

    // And set the primary_sale_happened flag on the metaplex metadata, which isn't strictly necessary but just
    // in case there are UI presentations that care
    ret = set_metaplex_metadata_primary_sale_happened(entry, params->ka, params->ka_num);
    if (ret) {
        return ret;
    }
    
    // Finally, close the entry's token account since it will never be used again.  The lamports go to the admin
    // account.
    return close_entry_token(block, entry, admin_account, params->ka, params->ka_num);
}
