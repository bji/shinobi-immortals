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
    // enters an auction period, with parameters defined by [minimum_price_lamports] and [duration].  If this is
    // false, then when an entry is first revealed, if it is has not been sold already as a mystery, it is immediately
    // purchasable for a price that starts at [final_start_price_lamports] and decays to [minimum_price_lamports] over
    // [duration].
    bool has_auction;

    // This is the duration to use for auctions and for final sale periods.
    uint32_t duration;

    // This is the initial price to sell revealed entry for during its final sale period, which must be
    // >= minimum_price_lamports.
    uint64_t final_start_price_lamports;

    // Blocks may be configured to only allow purchasing of Entries by accounts that are whitelisted.  Each block has
    // at most one whitelist: a Whitelist account (see inc/whitelist.h) at the program derived address for the block,
    // holding a list of up to MAX_WHITELIST_ENTRIES account pubkeys.  The admin adds pubkeys to it with the
    // AddWhitelistEntries instruction, which is only possible before the block is created; so the whitelist is fixed
    // from the moment that the block is created.  A block with a nonzero whitelist_duration can only be created if its
    // whitelist already exists and is non-empty (CreateBlock fails with Error_WhitelistRequired otherwise).
    //
    // If whitelist_duration is 0, then no whitelist is ever used for this block.  Otherwise, the whitelist period
    // lasts from the time that the block is complete (block_start_timestamp, which is when its last entry is added)
    // until whitelist_duration seconds after that.  During the whitelist period, Buy requires that the account that
    // pays for the purchase (the funding account) be in the whitelist, and removes it from the whitelist.  A pubkey
    // that appears in the whitelist more than once can buy that many entries.  Once every pubkey has been used, the
    // whitelist is empty, and anyone may buy for the remainder of the whitelist period; likewise if that empty
    // whitelist is then deleted.  After the whitelist period, anyone may buy.
    //
    // The admin can delete the whitelist (its lamports go to the admin) with the DeleteWhitelist instruction.  An
    // empty whitelist can always be deleted, as can any whitelist before its block is created.  Once the block is
    // created, a non-empty whitelist cannot be deleted until the whitelist period is over: not while the block's
    // entries are still being added, and not during the whitelist period itself.  So once a block that uses a
    // whitelist exists, its whitelist stays in effect for the whole whitelist period.
    //
    // Note that whitelist_duration alone does not say whether a whitelist is in effect.  A tool that wants to know
    // that must do what Buy does: check that a valid, non-empty whitelist exists at the block's whitelist address, and
    // that the whitelist period has not ended.
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
    // the block is complete and the mystery phase begins.  It is also the beginning of the life span of each of the
    // block's entries, over which their Ki harvests are reduced (see ki_harvest_reduction_per_day in EntryMetadata).
    timestamp_t block_start_timestamp;

    // This is the total number of mysteries which have been sold
    uint16_t mysteries_sold_count;

    // This is the timestamp that the number of mysteries sold became equal to the total_mystery_count, at which
    // time the reveal grace period begins.  If there were no mysteries for this block, then this is the timestamp
    // at which the last entry was added.  If the mystery phase instead ends because mystery_phase_duration elapses
    // with mysteries unsold, this stays 0; use get_mystery_phase_end_timestamp() rather than reading it directly.
    timestamp_t mystery_phase_end_timestamp;

    // This is a bitmap of all entries which have been added to the block.  If a bit is 1, the entry has been
    // added already; if it is 0, the entry has not been added.  This allows entries to be added in parallel.
    uint8_t entries_added_bitmap[0];

} Block;
