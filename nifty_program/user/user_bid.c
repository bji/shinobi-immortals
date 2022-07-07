#pragma once

#include "util/util_accounts.c"


// Account references:
// 0. `[WRITE, SIGNER]` -- The bidding account (which will supply the SOL for the bid, and also must sign subsequent
//                         claim re-bid or claim transactions)
// 1. `[]` -- The block account
// 2. `[WRITE]` -- The entry account
// 3. `[WRITE]` -- The bid PDA account
// 4. `[]` -- The system program id, for cross-program invoke
// 5. `[]` -- The nifty program, so it can call itself for create_pda

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


// This is guaranteed accurate for a 3 day auction and a current_max_bid of 10,000 SOL.  Above those values, accuracy
// is not guaranteed due to overflow.
static uint64_t compute_minimum_bid(uint64_t auction_duration, uint64_t initial_minimum_bid, uint64_t current_max_bid,
                                    uint64_t seconds_elapsed)
{
    // If there have been no bids yet, then the current max bid is the initial minimum
    if (current_max_bid < initial_minimum_bid) {
        return initial_minimum_bid;
    }

    // Sanitize the seconds elapsed
    if (seconds_elapsed > auction_duration) {
        seconds_elapsed = auction_duration;
    }

    // This is a curve based on the formula: y = p * ((1 / (101 - (100 * (a / b)))) + 1.09)
    // Where a is seconds_elapsed, b is auction_duration, and p is current_max_bid.  This is a curve that goes
    // from a multiple of 1.1 of the current_max_bid at time 0, up to approx 2x the current_max_bid at the end
    // of the time range.
    uint64_t a = seconds_elapsed;
    uint64_t b = auction_duration;
    uint64_t p = current_max_bid;

    uint64_t result = (p * (((1000 * b) / ((b + (b / 100)) - a)) + 109000)) / 100000;

    // result can be off if there is overflow.  So bound it by between 1.1 and 2.09 of the current_max_bid
    uint64_t min_result = current_max_bid + (current_max_bid / 10);
    uint64_t max_result = (2 * current_max_bid) + ((current_max_bid * 9) / 100);
    
    if (result < min_result) {
        return min_result;
    }
    else if (result > max_result) {
        return max_result;
    }
    else {
        return result;
    }
}


static uint64_t user_bid(SolParameters *params)
{
    // Sanitize the accounts.  There must be exactly 6.
    if (params->ka_num != 6) {
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

    // Check to make sure data is sane
    if (data->minimum_bid_lamports > data->maximum_bid_lamports) {
        return Error_InvalidData_First;
    }

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

    // Ensure that the bid account is the correct address for this bid
    SolPubkey computed_bid_account_address;
    if (!compute_bid_address(bidding_account->key, block->config.group_number, block->config.block_number,
                             entry->entry_index, data->bid_account_bump_seed, &computed_bid_account_address) ||
        !SolPubkey_same(&computed_bid_account_address, bid_account->key)) {
        return Error_InvalidAccount_First + 3;
    }

    // Get the clock sysvar, needed below
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }
    
    // Check to make sure that the entry is in a normal auction
    if (get_entry_state(block, entry, &clock) != EntryState_InNormalAuction) {
        return Error_EntryNotInAuction;
    }

    // Compute the minimum auction bid price for the entry
    uint64_t minimum_bid = compute_minimum_bid(block->config.auction_duration, block->config.minimum_price_lamports,
                                               entry->auction.highest_bid_lamports,
                                               clock.unix_timestamp - entry->auction.begin_timestamp);

    // If the minimum bid is greater than the maximum range of this bid, then the bid is not large enough
    if (minimum_bid > data->maximum_bid_lamports) {
        return Error_BidTooLow;
    }

    // Update minimum_bid to hold the minimum bid allowed by the range of this bid
    if (minimum_bid < data->minimum_bid_lamports) {
        minimum_bid = data->minimum_bid_lamports;
    }

    // Now minimum_bid is the actual bid

    // Create a PDA account for the bid holding the lamports of the bid, or update an existing account
    uint8_t prefix = PDA_Account_Seed_Prefix_Bid;
    SolSignerSeed seeds[] = { { &prefix, sizeof(prefix) },
                              { (uint8_t *) bidding_account->key, sizeof(*(bidding_account->key)) },
                              { (uint8_t *) &(block->config.group_number), sizeof(block->config.group_number) },
                              { (uint8_t *) &(block->config.block_number), sizeof(block->config.block_number) },
                              { (uint8_t *) &(entry->entry_index), sizeof(entry->entry_index) },
                              { &(data->bid_account_bump_seed), sizeof(data->bid_account_bump_seed) } };
    uint64_t ret = create_pda(bid_account, seeds, sizeof(seeds) / sizeof(seeds[0]), bidding_account->key,
                              &(Constants.nifty_program_id), minimum_bid, 0, params->ka, params->ka_num);
    if (ret) {
        return ret;
    }

    // Update the entry's auction details
    entry->auction.highest_bid_lamports = minimum_bid;

    sol_memcpy(&(entry->auction.winning_bid_pubkey), bid_account->key, sizeof(SolPubkey));

    // Not done yet
    return 0;
}
