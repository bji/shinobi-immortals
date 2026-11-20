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
    EntryState_OwnedAndStaked             = 9,
    // Entry's metadata has been reauthorized to another program.  This is a terminal state: the entry is retired, and
    // this program will not perform any further operation on it, other than letting losing bidders reclaim their bids.
    EntryState_Reauthorized               = 10
} EntryState;


typedef struct
{
    // Form at this level
    uint32_t form;

    // Attack and defense skill (top 4 bits = attack, bottom 4 = defense)
    uint8_t skill;

    // This is the number of Ki tokens earned per 1 SOL of stake rewards earned by stake accounts staked to the entry.
    uint32_t ki_factor;

    // Name of the entry at each level.  32 bytes is the Metaplex metadata maximum name length.  The name is padded
    // with zero bytes; a 32 byte name has no terminating zero.
    uint8_t name[32];

    // Title of the entry at each level: an additional line of identifying text, for example "Master Swordsman" for an
    // entry named "Harajuki Katsudo".  It is not part of the Metaplex metadata, so wallets will not show it, but game
    // UIs can.  Like the name, it is padded with zero bytes, and a 32 byte title has no terminating zero.
    uint8_t title[32];

    // Metadata URL of the entry at this level (metaplex metadata uri length is 200)
    uint8_t uri[200];

    // SHA-256 of the contents of the Metadata URL at this level.  Used to both verify the metadata and also
    // to allow alternate methods (outside of the scope of this program) for fetching metadata
    sha256_t uri_contents_sha256;

} LevelMetadata;


typedef struct
{
    // Number of whole level credits to achieve level 1.  Each subsequent level requires 50% more level credits to
    // achieve than the previous.
    uint32_t level_1_credits;

    // These are random numbers that can be used by other programs.  These values are randomly generated per entry.
    uint32_t random[16];

    // Metadata for each of the 9 levels
    LevelMetadata level_metadata[9];

    // The expected annual stake reward rate, in basis points (1/100ths of a percent), of a stake account delegated to
    // the Shinobi Systems validator, as estimated for the year following this entry's creation.  This is the rate at
    // which the stake account's balance is expected to grow from staking rewards (i.e. after validator commission).
    // It calibrates the level credits curve: combined with the number of level credit windows per year, it gives the
    // per-window Ki production of a 50 effective SOL stake account, which is the contribution rate that levels an
    // entry fully in about one year.  It must be non-zero for the entry to be levelled up.
    uint32_t expected_annual_reward_bps;

    // The fraction by which this entry's Ki harvests are reduced for each day of its life span, as a fraction of
    // UINT32_MAX (so 0 is no reduction at all, and UINT32_MAX is a reduction of everything).  An entry's life span
    // begins when its block was completed (the block's block_start_timestamp) and ends at the current time
    // (Clock.unix_timestamp); a harvest made when the entry's life span is n whole days pays
    //   (1 - (ki_harvest_reduction_per_day / UINT32_MAX))^n
    // of what it would otherwise pay (see compute_ki_harvest_retention in util_calculations.c).  So all of an entry's
    // Ki production slowly declines over its lifetime, which bounds the total Ki that it can ever produce.  Measuring
    // the life span in days of wall-clock time, rather than in epochs, means that the rate of decline does not depend
    // upon the length of epochs, which can change.  For example, about 603,500 reduces harvests by about 5% per year.
    // 0 means that harvests are never reduced; UINT32_MAX means that nothing is harvested once the life span is at
    // least one day.
    uint32_t ki_harvest_reduction_per_day;

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
    // time, which must be > 0.  It starts as the block's auction duration, and Bid increases it whenever a bid is
    // placed near the end of the auction (see AUCTION_EXTENSION_SECONDS in constants.h), so it is the length of the
    // auction so far.
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

    // This is set to true by ReAuthorize when it moves the entry's Metaplex metadata update authority away from this
    // program, and is never cleared.  Once it is set, the entry is in the EntryState_Reauthorized state: it is retired
    // from this program, which will not stake it, level it up, or operate on it in any other way, since the entry is
    // from then on managed by whichever program received its metadata.
    bool reauthorized;

    // If has_auction is true, then this struct is used
    struct {
        // If this entry is in an auction, then this is the current highest auction bid for this entry, or 0
        // if no bids have been received.  The first bid must be at least the entry's minimum price, and every later
        // bid must be at least MINIMUM_BID_INCREMENT_BPS (see util_calculations.c) higher than the previous bid
        uint64_t highest_bid_lamports;

        // Bid account address of the highest bid.  It is necessary to store this here to track what the winning
        // bid is; cannot rely just on the lamports value of the bid account since someone could send SOL into that
        // via a system transfer after it is created.
        SolPubkey winning_bid_pubkey;

    } auction;

    // If the entry is owned, then this struct is used
    struct {
        // The stake account that this Entry holds, all zeroes if none
        SolPubkey stake_account;

        // Lamports in the stake account at which Ki was most recently harvested
        uint64_t last_ki_harvest_stake_account_lamports;

    } owned;

    // Current level of the entry
    uint8_t level;

    // This is the entry's metadata
    EntryMetadata metadata;

    // The level credit window (Clock.unix_timestamp / LEVEL_CREDIT_WINDOW_SECONDS) in which ki_applied_this_window
    // was most recently changed.
    uint64_t most_recent_ki_applied_window;

    // Cumulative Ki contributed in that window.
    // Multiple contributions earn P(new total) - P(old total), preventing transaction splitting.
    uint64_t ki_applied_this_window;

    // Lifetime level credits in tenths of a credit. Never deducted or reset on level-up or transfer.
    uint64_t total_level_credits;

    // The number of whole epochs for which this entry has been staked with stake that was active and delegated to the
    // Shinobi Systems validator: epochs during the whole of which the entry held the stake account, and in which that
    // stake account's delegation to Shinobi Systems was effective.  This is a permanent record of the entry's history
    // that other programs (for example, games) may use; it belongs to the entry, not its owner, so it is never reset,
    // including when the entry is transferred.
    //
    // It is brought up to date only by instructions that examine the entry's stake account: Harvest, LevelUp (of a
    // staked entry), Destake, Delegate, ReAuthorize, and UpdateEpochsStaked, which exists only to do this (see
    // count_epochs_staked in util_ki.c).  So while the entry is staked, epochs since epochs_staked_counted_to_epoch may
    // not have been counted yet.  A reader that needs the exact value can execute UpdateEpochsStaked earlier in the
    // same transaction, or else count them itself from the stake account, in the same way.
    uint64_t epochs_staked;

    // Every epoch before this one has already been considered for epochs_staked, and will never be counted again.
    // Stake sets it to the epoch after the one in which the entry is staked, so that only epochs during the whole of
    // which the entry held the stake account are counted.  Only meaningful while the entry is staked.  (This is not
    // part of owned, because owned is zeroed by Destake.)
    uint64_t epochs_staked_counted_to_epoch;

} Entry;
