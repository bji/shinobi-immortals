#pragma once

#include "util/util_accounts.c"
#include "util/util_bid.c"
#include "util/util_math.c"
#include "util/util_calculations.c"

typedef struct
{
    // This is the instruction code for Bid
    uint8_t instruction_code;

    // Minimum bid in lamports
    uint64_t minimum_bid_lamports;

    // Maximum bid in lamports
    uint64_t maximum_bid_lamports;

} BidData;


static uint64_t user_bid(const SolParameters *params)
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
        DECLARE_ACCOUNT(7,  self_program_account,          ReadOnly,   NotSigner,  KnownAccount_SelfProgram);
        DECLARE_ACCOUNT(8,  spl_token_program_account,     ReadOnly,   NotSigner,  KnownAccount_SPLTokenProgram);
    }
    DECLARE_ACCOUNTS_NUMBER(9);

    // Make sure that the input data is the correct size
    if (params->data_len != sizeof(BidData)) {
        return Error_InvalidDataSize;
    }

    // Cast to instruction data
    const BidData *data = (BidData *) params->data;

    // Check to make sure data is sane
    if (data->minimum_bid_lamports > data->maximum_bid_lamports) {
        return Error_InvalidData_First;
    }

    // This is the entry data
    Entry *entry = get_validated_entry(entry_account);
    if (!entry) {
        return Error_InvalidAccount_First + 1;
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
    uint64_t minimum_bid = compute_minimum_bid(entry->minimum_price_lamports, entry->auction.highest_bid_lamports);

    // If the minimum bid is 0, then no bid is possible
    if (minimum_bid == 0) {
        return Error_BidTooLow;
    }

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

    // If this bid was placed with fewer than AUCTION_EXTENSION_SECONDS left in the auction, extend the auction so that
    // it ends AUCTION_EXTENSION_SECONDS after this bid.  The auction's end is reveal_timestamp + duration everywhere
    // that it is used, so extending it is only a matter of increasing duration; the auction is never shortened.  The
    // entry is in auction (checked above), so the current time is before the current end.  The sums are computed in
    // int64_t from values far below its limits: unix timestamps, a uint32_t duration, and a small constant.
    int64_t auction_end = entry->reveal_timestamp + ((int64_t) entry->duration);
    int64_t extended_end = clock.unix_timestamp + ((int64_t) AUCTION_EXTENSION_SECONDS);
    if (extended_end > auction_end) {
        int64_t extended_duration = extended_end - entry->reveal_timestamp;
        // duration is a uint32_t, so an auction can last at most UINT32_MAX seconds (over 136 years).  Rather than
        // let the duration wrap, which would move the end backwards, extend it only as far as that limit; the auction
        // then ends at the latest time an entry can record.  Each extension requires a bid at least
        // MINIMUM_BID_INCREMENT_BPS higher than the last, so this cannot happen in practice.
        if (extended_duration > ((int64_t) UINT32_MAX)) {
            extended_duration = (int64_t) UINT32_MAX;
        }
        entry->duration = (uint32_t) extended_duration;
    }

    // Not done yet
    return 0;
}
