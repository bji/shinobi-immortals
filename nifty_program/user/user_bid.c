#pragma once

#include "util/util_accounts.c"


// Bid:
// - Check to make sure entry is in auction
// - Bid will provide a price range.  This is because the time that tx lands is indeterminate and the maximum bid
//   may increase over time, so need to establish a maximum bid, but allow the bid to be less if possible
// - So then compute minimum possible bid price.  If the minimum possible price is > the maximum bid, then
//   fail.  Otherwise, bid price will be the minimum of [minimum possible price] and [supplied minimum bid]
// - Create a bid account that is a PDA of the bidding account, and write into it the details of the bid.  Transfer
//   the bid lamports to the bid account (minus the rent exempt minimum so that all lamports are accounted for).
// - Update the auction maximum bid with the new maximum bidder

// - Note that when outbid, to claim the outbidded-bid, must present the outbidded bid account, which will have
//   enough details to ensure that it is an outbidded-bid, and reclaim all SOL in that account to the user.
// - Note that bids are thus associated with specific system accounts, and are not tokens; bids can't be sold or
//   transferred to another wallet.
//
// - Re-bid: if the bid account already exists, then can just update its bid amount

// Account references:
// 0. `[WRITE, SIGNER]` -- The bidding account (which will supply the SOL for the bid, and also must sign subsequent
//                         claim re-bid or claim transactions)
// 1. `[]` -- The block account
// 2. `[WRITE]` -- The entry account
// 3. `[WRITE]` -- The bid PDA account
// 4. `[]` -- The system program id, for cross-program invoke

typedef struct
{
    // This is the instruction code for Bid
    uint8_t instruction_code;

    // Entry index of the entry being bid on
    uint16_t entry_index;

    // Bump seed for deriving the bid PDA
    uint8_t bid_account_bump_seed;

    // Minimum bid in lamports
    uint64_t minimum_bid_lamports;

    // Maximum bid in lamports
    uint64_t maximum_bid_lamports;
    
} BidData;



static uint64_t user_bid(SolParameters *params)
{
    // Sanitize the accounts.  There must be exactly 5.
    if (params->ka_num != 5) {
        return Error_IncorrectNumberOfAccounts;
    }

    SolAccountInfo *bidding_account = &(params->ka[0]);
    SolAccountInfo *block_account = &(params->ka[1]);
    SolAccountInfo *entry_account = &(params->ka[2]);
    SolAccountInfo *bid_account = &(params->ka[3]);
    // The account at index 4 must be the system program, but this is not checked; if it's not that program, then
    // the transaction will simply fail when the lamports transfer happens later

    // Make sure that the input data is the correct size
    if (params->data_len != sizeof(BidData)) {
        return Error_InvalidDataSize;
    }

    // Cast to instruction data
    BidData *data = (BidData *) params->data;

    // Check permissions
    if (!bidding_account->is_writable || !bidding_account->is_signer) {
        return Error_InvalidAccountPermissions_First;
    }
    if (!entry_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 2;
    }
    if (!bid_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 3;
    }

    // This is the block data
    Block *block = get_validated_block(block_account);
    if (!block) {
        return Error_InvalidAccount_First + 4;
    }

    // Ensure that the block is complete; cannot bid on anything from a block that is not complete yet
    if (!is_block_complete(block)) {
        return Error_BlockNotComplete;
    }

    // This is the entry data
    Entry *entry = get_validated_entry(block, data->entry_index, entry_account);
    if (!entry) {
        return Error_InvalidAccount_First + 2;
    }

    // Ensure that the bid account is the correct account for the bid:
    // - At correct address
    // - Owned by the nifty program
    SolPubkey computed_bid_account_address;
    if (!compute_bid_address(bidding_account->key, block->config.group_number, block->config.block_number,
                             entry->entry_index, data->bid_account_bump_seed, &computed_bid_account_address) ||
        !SolPubkey_same(&computed_bid_account_address, bid_account->key) ||
        !is_nifty_program(bid_account->owner)) {
        return Error_InvalidAccount_First + 3;
    }

    // Get the clock sysvar, needed below
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }
    
    // Check to make sure that the entry is in auction
    if (get_entry_state(block, entry, &clock) != EntryState_InAuction) {
        return Error_EntryNotInAuction;
    }

    // Not done yet
    return 2000;
}
