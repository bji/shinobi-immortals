#pragma once

// These are all of the states that an Entry can be in
typedef enum EntryState
{
    // Entry has not been revealed yet, and is not yet owned
    EntryState_PreRevealUnowned           = 0,
    // Entry is owned, but the containing block has not met its reveal criteria yet
    EntryState_PreRevealOwned             = 1,
    // The block containing the entry has met the reveal criteria, but the entry has not been revealed yet; it's unowned
    EntryState_WaitingForRevealUnowned    = 2,
    // The block containing the entry has met the reveal criteria, but the entry has not been revealed yet; it's owned
    EntryState_WaitingForRevealOwned      = 3,
    // Entry is in normal auction
    EntryState_InNormalAuction            = 4,
    // Entry is past its auction and waiting to be claimed
    EntryState_WaitingToBeClaimed         = 5,
    // Entry is past its auction end period but is not owned
    EntryState_Unowned                    = 6,
    // Entry is owned and revealed, but not staked
    EntryState_Owned                      = 7,
    // Entry is owned, revealed, and staked
    EntryState_OwnedAndStaked             = 8
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

    // Index of this entry within its block
    uint16_t entry_index;

    // Program Derived Address of the entry token mint
    ProgramDerivedAddress mint_account;

    // Program Derived Address of this program's token account for holding the token when the entry is unowned and
    // unsold
    ProgramDerivedAddress token_account;
    
    // Program Derived Address of the metaplex metadata account
    ProgramDerivedAddress metaplex_metadata_account;

    // Before the entry is revealed, this holds the SHA-256 of the following values concatenated together:
    // - The SHA-256 of the Entry metadata that will be supplied by the reveal transaction
    // - 8 bytes of salt
    // After the entry is revealed, this holds all zeroes
    sha256_t reveal_sha256;

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

    // If entry is in or was in auction, then this struct is used
    struct {
        // The auction begin time
        timestamp_t begin_timestamp;

        // If this entry is in a normal auction, then this is the current highest auction bid for this entry, or 0 if
        // no bids have been received.  The next highest bid must be at least the minimum bid for all entries of this
        // block (block.minimum_bid_lamports), and the "bid increment factor" (which is a function of the time left in
        // the auction) higher than the previous bid
        uint64_t highest_bid_lamports;

        // Bid account address of the highest bid.  It is necessary to store this here to track what the winning
        // bid is; cannot rely just on the lamports value of the bid account since someone could send SOL into that
        // via a system transfer after it is created.
        SolPubkey winning_bid_pubkey;
    } auction;

    // If entry state is EntryState_Staked, this struct is used
    struct {
        // EntryState_Staked: The stake account that this Entry holds, all zeroes if none
        SolPubkey stake_account;

        // Number of lamports in the stake account at the time of its last commission collection
        uint64_t last_commission_charge_stake_account_lamports;

        // Lamports in the stake account at which Ki was most recently harvested
        uint64_t last_ki_harvest_stake_account_lamports;
    } staked;

    // Excess commission owed in lamports.  This is added to when the "redelegation crank" is turned on
    // undelegated or not-delegated-to-Shinobi stake accounts.  Then when commission is next charged, these
    // lamports are also charged in addition to normal commission.
    uint64_t extra_commission_lamports;

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
