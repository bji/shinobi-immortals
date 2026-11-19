#pragma once

#include "util/util_accounts.c"
#include "util/util_bid.c"
#include "util/util_rent.c"
#include "util/util_token.c"

// Settles the winning bid of an auction that has ended: the entry's token goes to the winning bidder, the winning
// bid goes to the admin, and the entry becomes Owned.
//
// The winning bidder may do this at any time after the auction has ended; this is how the winner of an auction claims
// their win.  Once SETTLE_WINNING_BID_DELAY_SECONDS (30 days) have passed since the end of the auction, anyone else
// may do it too, and no signature is required from the bidder.  This is so that an auction which was won can always
// be completed.  If only the winning bidder could do it, then a bidder who lost their key, or who never returned,
// would leave the admin unpaid, the bid's lamports stuck in the bid account, and the entry stuck in the
// WaitingToBeClaimed state where it can never be bought, all permanently.  The delay leaves the winner time to claim
// their win themselves, rather than having someone else silently do it for them.
//
// Because the bidder need not sign, nothing here is under the control of whoever submits the transaction:
//
// - The token can only go to the associated token account of the bidder that is recorded in the bid account.
// - The winning bid can only go to the admin.
// - Anything that the bid account holds beyond the winning bid can only go to that bidder (see below).
//
// The only thing that the submitter of the transaction supplies is the funding account, which pays for the creation of
// the bidder's associated token account if it does not exist yet.  The submitter is the bidder if the funding
// account, which must sign, is the bidder's account.
//
// The amount of the winning bid is entry->auction.highest_bid_lamports, which only the Bid instruction sets, and not
// the balance of the bid account.  Anyone can transfer lamports to a bid account, and a bid account never holds less
// than its bid, so the bid account may hold more than the bid.  That excess is returned to the bidder, as it would
// have been by ClaimLosing had the bid lost, and the purchase price recorded in the entry is the winning bid itself.
//
// The bidder's bid marker token account for the entry has no purpose once the bid has been settled.  If the bidder is
// the one settling, then the bidder's signature is available, and the transaction may include the two optional
// accounts that allow the bid marker tokens to be burned and the bid marker token account to be closed, returning its
// lamports to the bidder, as ClaimLosing allows.  When someone else settles, this cannot be done, since it requires
// the bidder's signature; the bidder may burn the tokens and close the account themselves at any time.  It is optional
// for the bidder too, so that a bidder who has already burned or closed their bid marker can still settle.


static uint64_t anyone_settle_winning_bid(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,   funding_account,                  ReadWrite,  Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(1,   entry_account,                    ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,   bid_account,                      ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(3,   state_account,                    ReadOnly,   NotSigner,  KnownAccount_ProgramState);
        DECLARE_ACCOUNT(4,   admin_account,                    ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(5,   entry_token_account,              ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(6,   entry_mint_account,               ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(7,   authority_account,                ReadOnly,   NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(8,   token_destination_account,        ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(9,   bidder_account,                   ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(10,  system_program_account,           ReadOnly,   NotSigner,  KnownAccount_SystemProgram);
        DECLARE_ACCOUNT(11,  spl_token_program_account,        ReadOnly,   NotSigner,  KnownAccount_SPLTokenProgram);
        DECLARE_ACCOUNT(12,  spl_ata_program_account,          ReadOnly,   NotSigner,  KnownAccount_SPLATAProgram);
    }

    // If there are more than 13 accounts, then the optional reclaiming of bid marker is requested
    bool reclaim_bid_marker = (params->ka_num > 13);

    // If reclaiming bid marker, there must be 15 accounts
    if (reclaim_bid_marker) {
        DECLARE_ACCOUNTS_NUMBER(15);
    }
    // Else there must be 13
    else {
        DECLARE_ACCOUNTS_NUMBER(13);
    }

    // This instruction takes no data beyond the instruction code byte, so ensure that no data was supplied
    if (params->data_len != 1) {
        return Error_InvalidDataSize;
    }

    // This is the entry data
    Entry *entry = get_validated_entry(entry_account);
    if (!entry) {
        return Error_InvalidAccount_First + 1;
    }

    // Ensure that the correct admin account was passed in
    if (!is_admin_account(state_account, admin_account->key)) {
        return Error_InvalidAccount_First + 4;
    }

    // Check to make sure that the entry_token_account is the correct account for this entry
    if (!SolPubkey_same(entry_token_account->key, &(entry->token_pubkey))) {
        return Error_InvalidAccount_First + 5;
    }

    // Check to make sure that the entry_mint_account is the correct account for this entry
    if (!SolPubkey_same(entry_mint_account->key, &(entry->mint_pubkey))) {
        return Error_InvalidAccount_First + 6;
    }

    // Get the clock sysvar, needed below
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }

    // The only time that a winning bid could possibly be settled is if the entry is in the WaitingToBeClaimed state:
    // its auction has ended with a winning bid, and that bid has not been settled yet
    if (get_entry_state(0, entry, &clock) != EntryState_WaitingToBeClaimed) {
        return Error_CannotClaimBid;
    }

    // Get the validated bid account data
    const Bid *bid = get_validated_bid(bid_account);
    if (!bid) {
        return Error_InvalidAccount_First + 2;
    }

    // Only the winning bid of the entry can be settled
    if (!SolPubkey_same(bid_account->key, &(entry->auction.winning_bid_pubkey))) {
        return Error_CannotClaimBid;
    }

    // The bidder account must be the bidder that is recorded in the bid.  The bidder does not sign, so this is what
    // ensures that the token, and any excess lamports, go to the bidder and to nobody else.
    if (!SolPubkey_same(bidder_account->key, &(bid->bidder_pubkey))) {
        return Error_InvalidAccount_First + 9;
    }

    // The token destination account must be the bidder's associated token account for the entry's mint.  No other
    // destination is allowed, since the bidder is not here to choose one.
    SolPubkey bidder_token_pubkey;
    if (!compute_associated_token_address(bidder_account->key, &(entry->mint_pubkey), &bidder_token_pubkey) ||
        !SolPubkey_same(token_destination_account->key, &bidder_token_pubkey)) {
        return Error_InvalidAccount_First + 8;
    }

    // The bidder may settle at any time.  The bidder is the one settling if the funding account, which has signed, is
    // the bidder's account.  Anyone else must wait until SETTLE_WINNING_BID_DELAY_SECONDS have passed since the end
    // of the auction, which is the same end of auction that get_entry_state uses.
    if (!SolPubkey_same(funding_account->key, bidder_account->key) &&
        ((entry->reveal_timestamp + entry->duration + ((timestamp_t) SETTLE_WINNING_BID_DELAY_SECONDS)) >
         clock.unix_timestamp)) {
        return Error_OnlyBidderCanSettleYet;
    }

    // The bid marker can only be reclaimed if the bidder is the one settling, since burning the bid marker tokens and
    // closing the bid marker token account require the bidder's signature
    if (reclaim_bid_marker && !SolPubkey_same(funding_account->key, bidder_account->key)) {
        return Error_FailedToReclaimBidMarkerToken;
    }

    // Ensure that the token destination account exists.  If the account at the bidder's associated token account
    // address is already a token account for the entry's mint, then it is used as it is, whoever its owner is: an
    // associated token account always starts out owned by the bidder, and only the bidder could have given it to a
    // different owner.  Requiring it to still be owned by the bidder would let the bidder make settlement impossible.
    if (!is_token_account(token_destination_account, &(entry->mint_pubkey), 0, true)) {
        create_associated_token_account_idempotent(token_destination_account, &(entry->mint_pubkey),
                                                   bidder_account->key, funding_account->key,
                                                   params->ka, params->ka_num);
    }

    // Transfer the entry token to the destination account
    transfer_entry_token(entry, token_destination_account, params->ka, params->ka_num);

    // If the accounts were provided that would allow the bid marker token account to be reclaimed, do so
    if (reclaim_bid_marker) {
        {
            DECLARE_ACCOUNT(13,   bid_marker_mint_account,          ReadWrite,  NotSigner,  KnownAccount_NotKnown);
            DECLARE_ACCOUNT(14,   bid_marker_token_account,         ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        }

        // Burn the bid marker tokens and close the bid marker token account, returning its lamports to the bidder
        uint64_t ret = reclaim_bid_marker_token(&(entry->mint_pubkey), bidder_account, bid_marker_mint_account,
                                                bid_marker_token_account, params->ka, params->ka_num);
        if (ret) {
            return ret;
        }
    }

    // Set the purchase price on the entry to the winning bid amount, so that the entry now goes into an Owned state.
    entry->purchase_price_lamports = entry->auction.highest_bid_lamports;

    // As in user_buy, the Metaplex metadata's primary_sale_happened flag is deliberately not set (see user_buy.c for
    // why).  This keeps the Metaplex Metadata program out of the only path by which a winning bid can leave its bid
    // account: settling a winning bid depends on nothing beyond the System, SPL Token, and (only if the bidder's
    // associated token account does not exist yet) Associated Token Account programs.

    // Close the entry's token account, as user_buy does, since it is now empty and will never be used again.  Its
    // lamports go to the admin account.
    close_entry_token(entry, admin_account->key, params->ka, params->ka_num);

    // Return to the bidder whatever the bid account holds beyond the winning bid.  A bid account is funded with
    // exactly its bid, so there is only an excess if someone transferred lamports to the bid account.  This is done
    // after all cross-program invocations, as is the deletion of the bid account that follows.
    //
    // The excess is only returned if the bidder's account would be rent exempt after receiving it.  If the bidder's
    // account has been emptied, a small excess would leave it holding less than the rent exempt minimum, which would
    // make the whole transaction fail; anyone could then prevent settlement by transferring a single lamport to the
    // bid account.  In that case the excess is left in the bid account, and goes to the admin with the bid.
    if (*(bid_account->lamports) > entry->auction.highest_bid_lamports) {
        uint64_t excess_lamports = *(bid_account->lamports) - entry->auction.highest_bid_lamports;
        uint64_t bidder_lamports = *(bidder_account->lamports) + excess_lamports;
        // The first test guards against overflow of the sum
        if ((bidder_lamports >= excess_lamports) &&
            (bidder_lamports >= get_rent_exempt_minimum(bidder_account->data_len))) {
            *(bid_account->lamports) -= excess_lamports;
            *(bidder_account->lamports) = bidder_lamports;
        }
    }

    // Finally, delete the bid account, moving its remaining lamports, which are the winning bid, to the admin.  This
    // is done last, after all cross-program invocations, so that none of them sees the bid account in its deleted
    // state.
    delete_account(bid_account, admin_account);

    return 0;
}
