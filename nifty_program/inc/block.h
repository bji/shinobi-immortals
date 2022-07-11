#pragma once

#include "inc/data_type.h"
#include "inc/types.h"


// This is all configuration values that define the operational parameters of a block of entries.  These values are
// supplied when the block is first created, and can never be changed
typedef struct
{
    // Identification of the group number of this block
    uint32_t group_number;

    // Identification of the block number of this block within its group.
    uint32_t block_number;
    
    // Total number of entries in this block.  Must be greater than 0.
    uint16_t total_entry_count;

    // Number of mysteries which may be purchased; when all are purchased, the block can be revealed.  Must be equal to
    // or less than total_entry_count.
    uint16_t total_mystery_count;

    // Number of seconds to the timestamp of last entry add to get the end of the mystery phase.
    uint32_t mystery_phase_duration;

    // If total_mystery_count > 0, this is the cost in lamports of each mystery at the time that the mystery phase
    // began, and must be >= the minimum_price_lamports
    uint64_t mystery_start_price_lamports;

    // Number of seconds to add to the block state's all_mysteries_sold_timestamp value to get the cutoff period for
    // reveal; any entry which has not been revealed by this point allows zero-penalty return of the purchase price
    // (while retaining the mystery NFT itself).
    uint32_t reveal_period_duration;

    // This is a number of seconds to add to an auction start time to get the end of auction time.  May be 0, in which
    // case there is no auction at all, and instead the price of revealed Entries is always
    // auction_end_price_lamports.
    uint32_t auction_duration;

    // If this is true, then a "reverse" auction is performed.  If this is false, then a "normal" auction is
    // performed.  A normal auction starts with no bids and a minimum bid price of [final_mystery_price_lamports].
    // Each bid must be some multiple higher than the previous bid; this is determined by a formula that starts
    // at 1.1 and towards the end of the auction duration rapidly increases; this prevents "sniping" of bids near
    // the end of the auction.  In a normal auction, once the end of the auction is reached, if there are no bids,
    // then the price remains at [final_mystery_price_lamports] forever thereafter.
    // A reverse auction is identical in function to the mystery phase pricing: the price starts at
    // [reverse_auction_start_price_lamports] and decreases on a curve until it is
    // [reverse_auction_end_price_lamports]; it stays at that value forever thereafter.  Anyone can instantly buy
    // an entry during a reverse auction at the current reverse auction price; there is no "bidding", just immediate
    // purchase.
    bool is_reverse_auction;

    // In the mystery phase, this is the final price of a mystery.  For a normal auction, this is the minimum initial
    // bid price.  For a reverse auction, this is the end price.  For both auction types, this is the price of the
    // entry if the entry was not bid on or did not otherwise sell.  if there is no auction, then this is the price of
    // the entry.
    uint64_t minimum_price_lamports;

    // For a reverse auction, this is the start price of the auction in lamports.
    uint64_t reverse_auction_start_price_lamports;

    // This is the extra commission charged when a stake account that is not delegated to Shinobi Systems is
    // either un-delegated or re-delegated via the redelegation crank.
    commission_t redelegate_crank_commission;

} BlockConfiguration;


// The state values of a block; these can be updated after a block is created
typedef struct
{
    // This is the total number of entries that have been added to the block thus far.
    uint16_t added_entries_count;

    // This is the timestamp that the last entry was added to the block and it became complete; at that instant,
    // the block is complete and the mystery phase begins.
    timestamp_t block_start_timestamp;

    // This is the total number of mysteries which have been sold
    uint16_t mysteries_sold_count;

    // This is the timestamp that the number of mysteries sold became equal to the total_mystery_count, at which
    // time the reveal grace period begins.
    timestamp_t reveal_period_start_timestamp;

    // Commission currently charged per epoch for staked entries.  This value can be updated but not more often than
    // once per epoch, and it can only be updated in maximum increments of 2%.  For any given entry, the change
    // will only take effect after the commission charge *after this change */.
    commission_t commission;

    // Epoch of the last time that the commission was changed
    uint64_t last_commission_change_epoch;

} BlockState;


// This is the format of data stored in a block account
typedef struct
{
    // This is an indicator that the data is a Block
    DataType data_type;
    
    // This is the configuration of the block.  It is never changed after the block is created.  Each entry of the
    // block contains a duplicate of this data in its config.
    BlockConfiguration config;

    // These are the state values of the block.
    BlockState state;

    // These are the bump seeds of the entries of the block, of which there are config.total_entry_count.  Each bump
    // seed can be used to derive the address of an entry.  These are stored as bump seeds instead of the actual entry
    // address to save space and thus allow blocks to hold more entries.
    uint8_t entry_bump_seeds[];
    
} Block;
