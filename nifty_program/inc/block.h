
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

    // Number of tickets which may be purchased; when all are purchased, the block can be revealed.  Must be equal to
    // or less than total_entry_count.
    uint16_t total_ticket_count;

    // Number of seconds to the timestamp of last entry add to get the end of the ticketing phase.
    uint32_t ticket_phase_duration;

    // Number of seconds to add to the block state's reveal_time value to get the cutoff period for reveal; any entry
    // which has not been revealed by this point allows zero-penalty return of the original stake account
    uint32_t reveal_grace_duration;

    // Cost in lamports of each ticket at the time that the ticketing phase began.  Cannot be zero.
    uint64_t start_ticket_price_lamports;

    // Cost in lamports of each ticket at the end of the ticketing phase.  Cannot be less than
    // start_ticket_price_lamports.  At any time during the ticketing phase, the cost of a ticket is
    // ((start_ticket_price_lamports - end_ticket_price_lamports) * (current_time - block_start_time)) /
    //    ticket_phase_duration.  Note that since ticket_phase_duration can be zero, this formula should not be
    //    used in that case.
    // This is also the bid minimum for any auction of an entry from this block.
    uint64_t end_ticket_price_lamports;

    // This is a number of seconds to add to an auction start time to get the end of auction time.
    uint32_t auction_duration;

    // This is the minimum percentage of lamports staking commission to charge for an NFT.  Which means that when an
    // NFT is returned, if the total commission charged via stake commission while the user owned the NFT is less than
    // minimum_accrued_stake_commission, then commission will be charged to make up the difference.
    commission_t minimum_accrued_stake_commission;

    // This is the commission charged for stake that is withdrawn below the initial stake account value of a stake
    // account that was used to purchase an entry.  Withdrawing down to this value is free, but withdrawing below this
    // value will charge this commission (thus if this value is 2%, then 2% of stake will be split off and given to
    // the admin whenever stake is withdrawn).  It is not allowed to withdraw stake down below the rent exempt minimum
    // of a stake account.
    commission_t principal_withdraw_commission;

    // This is the extra commission charged when a stake account that is not delegated to Shinobi Systems is
    // either un-delegated or re-delegated via the redelegation crank.
    commission_t redelegate_crank_commission;

    // This is the number of "stake earned lamports" per Ki that is earned.  For example, 1000 would mean that for
    // every 1000 lamports of SOL earned via staking, 1 Ki token is awarded.  Must be greater than 0.
    uint32_t ki_factor;

    // Fraction of Ki that can be replaced by SOL when levelling up.  For example, if this were 0.5, and if the total
    // amount of Ki needed to go from level 2 to level 3 were 1,000 Ki; and if the total Ki earnings were only 500,
    // then 500 Ki could be paid as an alternate cost for completing the level up.  This value is a fraction with
    // 0xFFFF representing 1.0 and 0x0000 representing 0.  If this value is 0xFFFF, then an owner of the NFT can just
    // directly pay Ki to level up regardless of cumulative Ki earnings.  If this value is 0, then the NFT cannot be
    // leveled up except by fulling earning all required Ki via stake rewards.
    uint16_t level_up_payable_fraction;

} BlockConfiguration;


// The state values of a block; these can be updated after a block is created
typedef struct
{
    // This is the total number of entries that have been added to the block thus far.
    uint16_t added_entries_count;

    // This is the timestamp that the last entry was added to the block and it became complete; at that instant,
    // the block is complete and the ticket phase begins.
    timestamp_t block_start_timestamp;

    // This is the total number of tickets which have been sold
    uint16_t tickets_sold_count;

    // This is the timestamp that the number of tickets sold became equal to the total_ticket_count, at which
    // time the reveal grace period begins.
    timestamp_t all_tickest_sold_timestamp;

    // Commission currently charged per epoch.  It is a binary fractional value, with 0xFFFF representing 100%
    // commission and 0x0000 representing 0% commission.  This value can be updated but not more often than once
    // per epoch, and if the updated commission is more than 5%, then it can only be updated in maximum increments
    // of 2%.  In addition, commission cannot be changed if any entry has a last_commission_charge_epoch that is
    // not the current epoch (i.e. all accounts must be up-to-date with commission charges).
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
    // seed can be used to derive the address of an entry mint, from which can then be derived the address of an
    // Entry.
    uint8_t entry_bump_seeds[];
    
} Block;


#endif // BLOCK_H
