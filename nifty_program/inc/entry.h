
#ifndef ENTRY_H
#define ENTRY_H


// All possible states of an entry
typedef enum
{
    // Entry has not been revealed yet.  The only allowed actions are:
    // - Buy ticket
    // - Refund ticket
    // - Reveal
    EntryState_PreReveal             = 0,
    // Entry has been revealed, and a ticket was purchased for it, but the ticket has not been redeemed yet.  The only
    // allowed action is:
    // - Redeem ticket
    EntryState_AwaitingTicketRedeem  = 1,
    // The Entry in either in an auction (if the current time is less than the end of auction time) or is waiting
    // for the winning bid to claim (if the auction end has passed and there was a winning bid) or is unsold
    // (if the auction end has passed and there was no winning bid)
    EntryState_Unsold                = 2,
    // The entry is owned and staked
    EntryState_Staked                = 3
} EntryState;


typedef struct
{
    // 10 stat numbers
    uint16_t stats[10];

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

    // Metadata for each of the 10 levels
    LevelMetadata level_metadata[10];
    
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
    
    // The overall state of the entry
    EntryState state;

    // This holds the SHA-256 of the following values concatenated together:
    // - The SHA-256 of the Entry metadata that will be supplied by the reveal transaction
    // - The SHA-256 of the Metaplex metadata that will be supplied by the reveal transaction
    // - 8 bytes of salt
    sha256_t reveal_sha256;
    
    // This holds the token account that the program uses to hold the token
    SolPubkey token_account;

    // EntryState_PreReveal: if ticket sold, this is ticket mint, else all zeroes
    // EntryState_AwaitingTicketRedeem: ticket mint
    SolPubkey ticket_mint;
    
    // EntryState_Unsold: if there was a highest bid for the current or most recently ended auction, this holds
    //   the mint of the bid; otherwise zero
    SolPubkey bid;
    
    // EntryState_Staked: The stake account that this Entry holds
    SolPubkey stake_account;

    // This union holds additional values depending on EntryState.
    union {
        // If entry_state is EntryState_Unsold, this struct is used
        struct {
            // The auction begin time
            timestamp_t auction_begin_timestamp;
            
            // The current maximum auction bid for this entry, or 0 if no bids have been received
            uint64_t maximum_bid_lamports;
        } unsold;

        // If entry_state is EntryState_Staked, this struct is used
        struct {
            // Number of lamports in the stake account at the time of its last commission collection
            uint64_t last_commission_charge_stake_account_lamports;

            // Total cumulative commission collected in lamports (reset to 0 whenever the entry goes from Staked to
            // Unsold)
            uint64_t total_commission_charged_lamports;
        } staked;
    };

    // Total Ki harvested (or paid for level-ups) over the lifetime of this entry.  This is what allows levelling up.
    uint64_t total_ki_harvested_or_paid_lamports;

    // Program id of the replacement metadata program which is used to update metadata.  If all zeroes, the nifty
    // program itself updates metadata.  It may be updated only by the owner of the entry, and only to a the "next
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
