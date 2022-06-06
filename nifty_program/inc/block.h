
#ifndef BLOCK_H
#define BLOCK_H

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

    // Number of seconds to add to the block state's all_mysteries_sold_timestamp value to get the cutoff period for
    // reveal; any entry which has not been revealed by this point allows zero-penalty return of the purchase price
    // (while retaining the mystery NFT itself).
    uint32_t reveal_period_duration;

    // If total_mystery_count > 0, this is the cost in lamports of each mystery at the time that the mystery phase
    // began, which must be >= minimum_bid_lamports.
    uint64_t initial_mystery_price_lamports;

    // Minimum auction bid for each entry.  Cannot be greater than or equal to
    // initial_mystery_price_lamports.  At any time during the mystery phase, the cost of a mystery is
    // end_mystery_price_lamports +
    //   (((initial_mystery_price_lamports - minimum_bid) * (current_time - block_start_time)) /
    //      mystery_phase_duration).  Note that since mystery_phase_duration can be zero, this formula should not be
    //    used in that case.
    uint64_t minimum_bid_lamports;

    // This is a number of seconds to add to an auction start time to get the end of auction time.
    uint32_t auction_duration;

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
    // once per epoch, and it can only be updated in maximum increments of 2%.
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


#endif // BLOCK_H
