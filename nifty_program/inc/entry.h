#pragma once

// These are all of the states that an Entry can be in
typedef enum EntryState
{
    // Entry has not been revealed yet -- the specific pre-reveal state is not known
    EntryState_PreReveal                  = 0,
    // Entry has not been revealed yet, and is not yet owned
    EntryState_PreRevealUnowned           = 1,
    // Entry is owned, but the containing block has not met its reveal criteria yet
    EntryState_PreRevealOwned             = 2,
    // The block containing the entry has met the reveal criteria, but the entry has not been revealed yet; it's unowned
    EntryState_WaitingForRevealUnowned    = 3,
    // The block containing the entry has met the reveal criteria, but the entry has not been revealed yet; it's owned
    EntryState_WaitingForRevealOwned      = 4,
    // Entry is in auction
    EntryState_InAuction                  = 5,
    // Entry is past its auction and waiting to be claimed
    EntryState_WaitingToBeClaimed         = 6,
    // Entry is past its auction end period but is not owned
    EntryState_Unowned                    = 7,
    // Entry is owned and revealed, but not staked
    EntryState_Owned                      = 8,
    // Entry is owned, revealed, and staked
    EntryState_OwnedAndStaked             = 9
} EntryState;


typedef struct
{
    // Form at this level
    uint8_t form;
    
    // Attack and defense skill (top 4 bits = attack, bottom 4 = defense)
    uint8_t skill;

    // Special added at this level
    uint8_t special1;

    // Special added at this level
    uint8_t special2;

    // This is the number of Ki tokens earned per 1 SOL of stake rewards earned by stake accounts staked to the entry.
    uint32_t ki_factor;

    // Name of the entry at each level (metaplex metadata maximum name length of 32), used to update metaplex
    // metadata with each level up.
    uint8_t name[32];

    // Image URL of the entry at each level (metaplex metadata uri length is 200), used to update metaplex
    // metadata with each level up.
    uint8_t uri[200];
    
} LevelMetadata;


typedef struct
{
    // Number of ki to achieve level 1.  Each subsequent level requires 50% more Ki to achieve than the previous.
    uint64_t level_1_ki;

    // Metadata for each of the 9 levels
    LevelMetadata level_metadata[9];
    
} EntryMetadata;


typedef struct
{
    // This is an indicator that the data is an Entry
    DataType data_type;

    // This is the address of the block that this entry is a part of.  It is stored here so that the address of
    // the block, when the block is provided in transactions, can be quickly verified.
    SolPubkey block_pubkey;

    // This is the group number of this Entry
    uint32_t group_number;

    // This is the block number of this Entry
    uint32_t block_number;

    // Index of this entry within its block
    uint16_t entry_index;

    // Address of the entry token mint
    SolPubkey mint_pubkey;

    // Address of this program's token account for holding the token when the entry is unowned and unsold
    SolPubkey token_pubkey;
    
    // Program Derived Address of the metaplex metadata account
    SolPubkey metaplex_metadata_pubkey;

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

    // If [has_auction] is true, this is a number of seconds to add to entry reveal time to get the end of auction
    // time, which must be > 0.
    // If [has_auction] is false, this is the number of seconds it takes for the entry price to decay from
    // non_auction_start_price_lamports to minimum_price_lamports
    uint32_t duration;
    
    // If [has_auction] is false, this is the initial price to sell revealed entry for, which must be
    // >= minimum_price_lamports.
    uint64_t non_auction_start_price_lamports;

    // Before the entry is revealed, this holds the SHA-256 of the following values concatenated together:
    // - The SHA-256 of the Entry metadata that will be supplied by the reveal transaction
    // - 8 bytes of salt
    // After the entry is revealed, this holds all zeroes
    sha256_t reveal_sha256;

    // Timestamp that the entry was revealed
    timestamp_t reveal_timestamp;
    
    // When an entry is purchased, this is the number of lamports it was purchased for.  If purchased before reveal,
    // then this number of lamports is temporarily held in the program authority account and returned to the user on a
    // Refund action, or transferred to the admin account on a Reveal action.  If purchased after reveal, then this
    // number of lamports is transferred directly to the admin account.
    uint64_t purchase_price_lamports;

    // If the entry was purchased as a mystery, but was not revealed before the end of the reveal grace period, the
    // user may request a refund, and the lamports that they used to purchase the entry will be returned to them.
    // If that happens, this value will be set to true.  This will prevent a subsequent attempt to refund the same
    // entry again.
    bool refund_awarded;

    // This is the commission to charge for this entry.  It is copied from the entry's block's commission when the
    // entry is first created, and after every commission charge to the entry.  This allows the block's commission
    // to be updated, and only take effect after all owed commission has already been charged at the prior commission.
    commission_t commission;

    // If has_auction is true, then this struct is used
    struct {
        // If this entry is in an auction, then this is the current highest auction bid for this entry, or 0
        // if no bids have been received.  The next highest bid must be at least the minimum bid for all entries
        // of this block (block.minimum_bid_lamports), and the "bid increment factor" (which is a function of the
        // time left in the auction) higher than the previous bid
        uint64_t highest_bid_lamports;
        
        // Bid account address of the highest bid.  It is necessary to store this here to track what the winning
        // bid is; cannot rely just on the lamports value of the bid account since someone could send SOL into that
        // via a system transfer after it is created.
        SolPubkey winning_bid_pubkey;
        
    } auction;

    // If the entry is owned, then this struct is used
    struct {
        // EntryState_Staked: The stake account that this Entry holds, all zeroes if none
        SolPubkey stake_account;
        
        // Lamports in the stake account at which Ki was most recently harvested
        uint64_t last_ki_harvest_stake_account_lamports;
        
        // Number of lamports in the stake account at the time of its last commission collection
        uint64_t last_commission_charge_stake_account_lamports;
        
    } owned;

    // Current level of the entry
    uint8_t level;

    // This is the entry's metadata
    EntryMetadata metadata;

} Entry;
