#pragma once

#include "util/util_bid.c"
#include "util/util_accounts.c"

typedef struct
{
    // This is the instruction code for Bid
    uint8_t instruction_code;

    // Minimum bid in lamports
    uint64_t minimum_bid_lamports;

    // Maximum bid in lamports
    uint64_t maximum_bid_lamports;
    
} BidData;


// This is guaranteed accurate for a 3 day auction and a current_max_bid of 10,000 SOL.  Above those values, accuracy
// is not guaranteed due to overflow.
static uint64_t compute_minimum_bid(uint64_t auction_duration, uint64_t initial_minimum_bid, uint64_t current_max_bid,
                                    uint64_t seconds_elapsed);


static uint64_t user_bid(SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,  bidding_account,               ReadWrite,  Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(1,  entry_account,                 ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,  bid_marker_mint_account,       ReadWrite,  NotSigner,  KnownAccount_BidMarkerMint);
        DECLARE_ACCOUNT(3,  bid_marker_token_account,      ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(4,  bid_account,                   ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(5,  authority_account,             ReadOnly,   NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(6,  system_program_account,        ReadOnly,   NotSigner,  KnownAccount_SystemProgram);
        DECLARE_ACCOUNT(7,  nifty_program_account,         ReadOnly,   NotSigner,  KnownAccount_NiftyProgram);
        DECLARE_ACCOUNT(8,  spl_token_program_account,     ReadOnly,   NotSigner,  KnownAccount_SPLTokenProgram);
    }
    DECLARE_ACCOUNTS_NUMBER(9);

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

    // This is the entry data
    Entry *entry = get_validated_entry(entry_account);
    if (!entry) {
        return Error_InvalidAccount_First + 2;
    }

    // Get the clock sysvar, needed below
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }
    
    // Check to make sure that the entry is in an auction
    if (get_entry_state(0, entry, &clock) != EntryState_InAuction) {
        return Error_EntryNotInAuction;
    }

    // Compute the minimum auction bid price for the entry
    uint64_t minimum_bid = compute_minimum_bid(entry->duration, entry->minimum_price_lamports,
                                               entry->auction.highest_bid_lamports,
                                               clock.unix_timestamp - entry->reveal_timestamp);

    // If the minimum bid is greater than the maximum range of this bid, then the bid is not large enough
    if (minimum_bid > data->maximum_bid_lamports) {
        return Error_BidTooLow;
    }

    // Update minimum_bid to hold the minimum bid allowed by the range of this bid
    if (minimum_bid < data->minimum_bid_lamports) {
        minimum_bid = data->minimum_bid_lamports;
    }

    // Now minimum_bid is the actual bid

    // If one doesn't already exist for this bid, mint a "bid marker" token that will allow user interfaces to
    // recognize that the user has an outstanding bid.  If the user loses this bid marker token, they can still claim
    // their bid but they have to know the mint address of the entry that was bid on, and from that compute the bid
    // marker token account, and from that compute the bid account.
    uint64_t ret = mint_bid_marker_token_idempotent(bid_marker_token_account, &(entry->mint_pubkey),
                                                    bidding_account->key, params->ka, params->ka_num);
    if (ret) {
        return ret;
    }
    
    // Create the bid account itself, which will hold the bid lamports in escrow and be claimable by a user_claim
    // instruction
    ret = create_entry_bid_account(bid_account, bid_marker_token_account->key, &(entry->mint_pubkey),
                                   bidding_account->key, minimum_bid, params->ka, params->ka_num);
    if (ret) {
        return ret;
    }

    // Update the entry's auction details
    entry->auction.highest_bid_lamports = minimum_bid;

    // Save the pubkey of the winning bid account, so that when bid claims are made after the end of auction, this one
    // will claim the entry.  All others will reclaim the SOL in the bid account.
    entry->auction.winning_bid_pubkey = *(bid_account->key);

    // Not done yet
    return 0;
}


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
