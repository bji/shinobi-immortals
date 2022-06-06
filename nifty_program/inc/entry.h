
#ifndef ENTRY_H
#define ENTRY_H

typedef enum EntryState
{
    // Entry has not been revealed yet, and is not yet owned
    EntryState_PreRevealUnowned  = 0,
    // Entry has been purchased, but not yet revealed
    EntryState_PreRevealOwned    = 1,
    // Entry has been revealed, and is not owned.  If is either in auction (if the auction end time has not
    // been reached yet) or is post-auction (if the auction timestamp has been reached).
    EntryState_Unsold            = 2,
    // Entry is owned and staked
    EntryState_Staked            = 3
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
    
    // This is the number of "stake earned lamports" per Ki that is earned.  For example, 1000 would mean that for
    // every 1000 lamports of SOL earned via staking, 1 Ki token is awarded.  If 0, no Ki is earned by staking this
    // entry.
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
    // Current level of the NFT
    uint8_t level;

    // Number of ki to achieve level 1.  Each subsequent level requires 50% more Ki to achieve than the previous.
    uint64_t level_1_ki;

    // Metadata for each of the 9 levels
    LevelMetadata level_metadata[9];

    // Current stats
    uint16_t stats[20];
    
} EntryMetadata;


typedef struct
{
    // This is an indicator that the data is an Entry
    DataType data_type;
    
    // This is identical to the config values in the block that this entry is a part of.  They are duplicated here
    // to allow transactions that otherwise do not need the block, to not have to read it.
    BlockConfiguration block_config;

    // Index of this entry within its block
    uint16_t entry_index;

    // Address of the entry token mint
    SolPubkey mint_account;

    // Address of this program's token account for holding the token when the entry is unowned and unsold
    SolPubkey token_account;
    
    // Address of the metaplex metadata account
    SolPubkey metaplex_metadata_account;

    // This holds the SHA-256 of the following values concatenated together:
    // - The SHA-256 of the Entry metadata that will be supplied by the reveal transaction
    // - 8 bytes of salt
    sha256_t reveal_sha256;

    // Overall state of the entry
    EntryState state;

    // This is the account holding the SOL used to purchase a mystery entry.  When the entry is revealed, the SOL in
    // this account is transferred to the admin account.  If the reveal period passes without a reveal, the owner
    // of the NFT may re-claim the SOL in this account.  If this is nonzero, the entry has been purchased and is
    // awaiting reveal.
    SolPubkey reveal_escrow_account;

    struct {
        // The auction begin time
        timestamp_t auction_begin_timestamp;
        
        // If the token is not user owned, then if there was a highest bid for the in-progress or ended auction, this
        // holds the mint of the bid; otherwise all zeroes
        SolPubkey highest_bid_mint;
    
        // The current highest auction bid for this entry, or 0 if no bids have been received.  The next highest bid
        // must be at least the minimum bid for all entries of this block (block.minimum_bid_lamports), and 10% higher
        // than the previous bid
        uint64_t highest_bid_lamports;
    } unsold;

    // If entry_state is EntryState_Staked, this struct is used
    struct {
        // EntryState_Staked: The stake account that this Entry holds, all zeroes if none
        SolPubkey stake_account;

        // Number of lamports in the stake account at the time of its last commission collection
        uint64_t last_commission_charge_stake_account_lamports;

        // Ki which was earned during commission charge and is waiting to be harvested
        uint64_t earned_ki;
    } staked;

    // Program id of the replacement metadata program which is used to update metadata.  If all zeroes, the nifty
    // program itself updates metadata.  It may be updated only by the owner of the entry, and only to the "next
    // value" in the sequence of metadata program ids listed in the program config.
    SolPubkey metadata_program;

    // These are the Shinobi metadata values for this entry.  These are all zeroes until the entry is revealed,
    // at which point the verified metadata is copied in.
    EntryMetadata metadata;

    // This is a metadata expansion area, which holds any expansion metadata created by an upgraded metadata program.
    // The entry is allocated with some amount of additional space here, and any replacement metadata program can read
    // it; it is updated with the results of that metadata program (including re-allocing this Entry if necessary to
    // accomodate the larger size).
    uint8_t metadata_expansion[];
    
} Entry;


#endif // ENTRY_H
