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
    // (while retaining the mystery token itself).
    uint32_t reveal_period_duration;

    // This value is used in three ways:
    // - It is the final price of the mystery at the end of this entry's mystery phase
    // - It is the starting price of an auction for this entry
    // - It is the final price of the entry if it is unsold and not a mystery and past its auction phase
    uint64_t minimum_price_lamports;

    // If this is true, then when an entry is first revealed, if it is has not been sold already as a mystery, then it
    // enters an auction period, with parameters defined by the values in [auction].  If this is false, then when an
    // entry is first revealed, if it is has not been sold already as a mystery, it is immediately purchasable for a
    // price that is determined by parameters in [non_auction];
    bool has_auction;

    // This is the duration to use for auctions and for final sale periods.
    uint32_t duration;

    // This is the initial price to sell revealed entry for during its final sale period, which must be
    // >= minimum_price_lamports.
    uint64_t final_start_price_lamports;

    // Blocks may be configured to only allow purchasing of Entries by system accounts that are whitelisted.  A system
    // account is whitelisted if there exists a PDA at the whitelist address for the { system account, block } tuple.
    // For blocks that have a whitelist, having such an account is a prerequisite for buying.  The whitelist PDA is
    // deleted (and its lamports returned to the admin) on a successful buy operation.  Whitelist entries are added
    // via the admin_create_whitelist_entry instruction.  Whitelist requirements expire after whitelist_duration
    // seconds have passed since block_start_timestamp, and at that time, whitelist entries that haven't been used yet
    // may be deleted by the admin using the admin_delete_whitelist_entry instruction.  If whitelist_duration is 0,
    // then no whitelist is ever used for this block.
    uint32_t whitelist_duration;

} BlockConfiguration;


// This is the format of data stored in a block account
typedef struct
{
    // This is an indicator that the data is a Block
    DataType data_type;

    // This is the configuration of the block.  It is never changed after the block is created.  Each entry of the
    // block contains a duplicate of this data in its config.
    BlockConfiguration config;

    // This is the total number of entries that have been added to the block thus far.
    uint16_t added_entries_count;

    // This is the timestamp that the last entry was added to the block and it became complete; at that instant,
    // the block is complete and the mystery phase begins.
    timestamp_t block_start_timestamp;

    // This is the total number of mysteries which have been sold
    uint16_t mysteries_sold_count;

    // This is the timestamp that the number of mysteries sold became equal to the total_mystery_count, at which
    // time the reveal grace period begins.  If there were no mysteries for this block, then this is the timestamp
    // at which the last entry was added.
    timestamp_t mystery_phase_end_timestamp;

    // Commission currently charged per epoch for staked entries.  This value can be updated but not more often than
    // once per epoch, and it can only be updated in maximum increments of 2%.  For any given entry, the change
    // will only take effect after the commission charge *after this change*.
    commission_t commission;

    // Epoch of the last time that the commission was changed
    uint64_t last_commission_change_epoch;

    // This is a bitmap of all entries which have been added to the block.  If a bit is 1, the entry has been
    // added already; if it is 0, the entry has not been added.  This allows entries to be added in parallel.
    uint8_t entries_added_bitmap[0];

} Block;
