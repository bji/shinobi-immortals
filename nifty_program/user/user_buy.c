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
// 7. `[WRITE]` -- The account to give ownership of the entry (token) to.  It must already exist and be a valid
//                 token account for the entry mint.
// 8. `[WRITE]` -- The metaplex metadata account for the entry
// 9. `[]` -- The nifty program, for cross-program invoke signature
// 10. `[]` -- The SPL token program, for cross-program invoke
// 11. `[]` -- The metaplex metadata program, for cross-program invoke
// 12. `[]` -- The system program id, for cross-program invoke

typedef struct
{
    // This is the instruction code for Buy
    uint8_t instruction_code;

    // This is the entry_index of the entry within its block
    uint16_t entry_index;
    
} BuyData;


// If start_price > 100,000 SOL, rounding errors could be significant.
static uint64_t compute_mystery_price(uint64_t total_seconds, uint64_t start_price, uint64_t end_price,
                                      uint64_t seconds_elapsed)
{
    uint64_t delta = start_price - end_price;

    // Sanitize seconds_elapsed
    if (seconds_elapsed > total_seconds) {
        seconds_elapsed = total_seconds;
    }

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
    // Sanitize the accounts.  There must be exactly 13.
    if (params->ka_num != 13) {
        return Error_IncorrectNumberOfAccounts;
    }

    SolAccountInfo *funding_account = &(params->ka[0]);
    SolAccountInfo *config_account = &(params->ka[1]);
    SolAccountInfo *admin_account = &(params->ka[2]);
    SolAccountInfo *authority_account = &(params->ka[3]);
    SolAccountInfo *block_account = &(params->ka[4]);
    SolAccountInfo *entry_account = &(params->ka[5]);
    SolAccountInfo *entry_token_account = &(params->ka[6]);
    SolAccountInfo *token_destination_account = &(params->ka[7]);
    SolAccountInfo *metaplex_metadata_account = &(params->ka[8]);
    // The account at index 9 must be the nifty program, but this is not checked; if it's not that program, then
    // the transaction will simply fail when it is used to sign a cross-program invoke later
    // The account at index 10 must be the SPL token program, but this is not checked; if it's not that program, then
    // the transaction will simply fail when it is invoked later
    // The account at index 11 must be the metaplex metadata program, but this is not checked; if it's not that
    // program, then the transaction will simply fail when the metadata is updated
    // The account at index 12 must be the system program, but this is not checked; if it's not that program, then
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

    // Make sure that the suppled admin account really is the admin account
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

    // Check that the correct metaplex metadata account is supplied
    if (!SolPubkey_same(metaplex_metadata_account->key, &(entry->metaplex_metadata_account.address))) {
        return Error_InvalidAccount_First + 8;
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

    case EntryState_InAuction:
        // In auction, can't be purchased
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
        purchase_price_lamports = compute_mystery_price(block->config.mystery_phase_duration,
                                                        block->config.initial_mystery_price_lamports,
                                                        block->config.minimum_bid_lamports,
                                                        clock.unix_timestamp - block->state.block_start_timestamp);

        // Update the entry's block to indicate that one more mystery was purchased.  If this is the last
        // mystery to purchase before the block becomes revealable, then the block reveal period begins.
        block->state.mysteries_sold_count += 1;
        if (block->state.mysteries_sold_count == block->config.total_mystery_count) {
            block->state.reveal_period_start_timestamp = clock.unix_timestamp;
        }

        break;

    case EntryState_Unowned:
        // Revealed but didn't sell at auction.  Can be purchased.

        // The destination of funds is the admin account
        funds_destination_account = admin_account;

        // Price is the minimum bid.
        purchase_price_lamports = block->config.minimum_bid_lamports;
        
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
