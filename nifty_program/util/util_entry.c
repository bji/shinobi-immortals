
#ifndef UTIL_ENTRY_C
#define UTIL_ENTRY_C

#include "inc/types.h"
#include "util_token.c"

static bool is_entry_revealed(Entry *entry)
{
    return is_all_zeroes(&(entry->reveal_sha256), sizeof(entry->reveal_sha256));
}


// Assumes that the entry is revealed
static bool is_entry_in_auction(Block *block, Entry *entry, Clock *clock)
{
    return (entry->unsold.auction_begin_timestamp &&
            ((entry->unsold.auction_begin_timestamp + block->config.auction_duration) > clock->unix_timestamp));
}


// Assumes that the block containing the Entry is complete
static EntryState get_entry_state(Block *block, Entry *entry, Clock *clock)
{
    // If the entry has been revealed ...
    if (is_entry_revealed(entry)) {
        // If the entry has been purchased ...
        if (entry->purchase_price_lamports) {
            // It's owned, and possibly staked
            if (is_all_zeroes(&(entry->staked.stake_account), sizeof(entry->staked.stake_account))) {
                return EntryState_Owned;
            }
            else {
                return EntryState_OwnedAndStaked;
            }
        }
        // Else the entry has not been purchased, but has been revealed.  If the auction is still ongoing, then it's
        // in auction
        else if (is_entry_in_auction(block, entry, clock)) {
            return EntryState_InAuction;
        }
        // Else it's no longer in auction, if it was bid on, then it's waiting to be claimed
        else if (entry->unsold.highest_bid_lamports) {
            return EntryState_WaitingToBeClaimed;
        }
        // Else it's no longer in auction, but never bid on.  It's simply not owned.
        else {
            return EntryState_Unowned;
        }
    }
    // Else the entry has not been revealed yet ... if the reveal criteria for the block has been met, then the entry
    // is waiting for reveal.  If a purchase price has been set, then the entry was purchased, and is owned and
    // waiting for reveal.
    else if (entry->purchase_price_lamports) {
        return EntryState_WaitingForRevealOwned;
    }
    // Else the entry has not been revealed and not purchased either.  If the entry's block has reached the reveal
    // criteria, then it's unowned and waiting for reveal.
    else if (is_complete_block_revealable(block, clock)) {
        return EntryState_WaitingForRevealUnowned;
    }
    // Else the entry is not revealed, not purchased, and not yet ready to be revealed
    else {
        return EntryState_PreRevealUnowned;
    }
}


#endif // UTIL_ENTRY_C
