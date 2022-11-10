#pragma once


static uint64_t user_claim_losing(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,   bidding_account,                  ReadWrite,  Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(1,   entry_account,                    ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,   bid_account,                      ReadWrite,  NotSigner,  KnownAccount_NotKnown);
    }

    // If there are more than 3 accounts, then the optional reclaiming of bid marker is requested
    bool reclaim_bid_marker = (params->ka_num > 3);

    // If reclaiming bid marker, there must be 7 accounts, else there must be 3
    if (reclaim_bid_marker) {
        DECLARE_ACCOUNTS_NUMBER(7);
    }
    else {
        DECLARE_ACCOUNTS_NUMBER(3);
    }

    // Get the validated entry account data
    const Entry *entry = get_validated_entry(entry_account);
    if (!entry) {
        return Error_InvalidAccount_First + 1;
    }

    // Get the clock sysvar, needed below
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }

    // Ensure that the entry is in the correct state for a losing bid claim to even be possible
    switch (get_entry_state(0, entry, &clock)) {
        // For the following, states, it's not possible for a bid to ever have been made
    case EntryState_PreReveal:
    case EntryState_PreRevealUnowned:
    case EntryState_PreRevealOwned:
    case EntryState_WaitingForRevealUnowned:
    case EntryState_WaitingForRevealOwned:
    case EntryState_Unowned:
        return Error_CannotClaimBid;

        // If the entry is in auction, then clearly there could have been losing bids
    case EntryState_InAuction:
        break;

        // For the following states, it is possible that there was an auction that this bidder bid on
    case EntryState_WaitingToBeClaimed:
    case EntryState_Owned:
    case EntryState_OwnedAndStaked:
        // If the entry has no auction, then it's not possible that this bidder bid on it
        if (!entry->has_auction) {
            return Error_CannotClaimBid;
        }
        break;
    }

    // Get the validated bid account data
    const Bid *bid = get_validated_bid(bid_account);
    if (!bid) {
        return Error_InvalidAccount_First + 2;
    }

    // If the bidder pubkey written into the bid is not the same as the bidder that was provided in this transaction,
    // then this is an invalid attempt to claim a bid
    if (!SolPubkey_same(bidding_account->key, &(bid->bidder_pubkey))) {
        return Error_InvalidAccount_First;
    }

    // If this is the winning bid, then can't claim it as a losing bid
    if (SolPubkey_same(bid_account->key, &(entry->auction.winning_bid_pubkey))) {
        return Error_BidWon;
    }

    // If the accounts were provided that would allow the bid marker token account to be reclaimed, do so
    if (reclaim_bid_marker) {
        {
            DECLARE_ACCOUNT(3,   bid_marker_mint_account,        ReadWrite,  NotSigner,  KnownAccount_NotKnown);
            DECLARE_ACCOUNT(4,   bid_marker_token_account,       ReadWrite,  NotSigner,  KnownAccount_NotKnown);
            DECLARE_ACCOUNT(5,   authority_account,              ReadOnly,   NotSigner,  KnownAccount_Authority);
            DECLARE_ACCOUNT(6,   spl_token_program_account,      ReadOnly,   NotSigner,  KnownAccount_SPLTokenProgram);
        }

        // Burn the bid marker tokens
        uint64_t ret = reclaim_bid_marker_token(&(entry->mint_pubkey), bidding_account, bid_marker_mint_account,
                                                bid_marker_token_account, params->ka, params->ka_num);
        if (ret) {
            return ret;
        }
    }

    // Move the bid account lamports to the bidding account
    *(bidding_account->lamports) += *(bid_account->lamports);
    *(bid_account->lamports) = 0;

    return 0;
}
