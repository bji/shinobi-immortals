
// This is a unix timestamp
typedef int64_t timestamp_t;

// This is a commission.  It is a binary fractional value, with 0xFFFF representing 100% commission and 0x0000
// representing 0% commission.
typedef uint16_t commission_t;

// 32 bytes of a SHA-256 value
typedef uint8_t sha256_t[32];

// This is a 10 byte seed value used to derive a PDA
typedef uint8_t seed_t[10];

// This is a 64 bitseed value used to make the SHA-256 of an entry unguessable
typedef uint64_t salt_t;


// This is a single value stored at the front of Program Derived Account of the nifty program, that indicates the type
// of data stored in the account.  Do not use 0 for any value here, since that is what will be present for accounts
// that do not exist and are supplied as all zeroes
typedef enum
{
    // This is a block of entries
    DataType_Block   = 1,

    // This is an auction bid
    DataType_Bid     = 2
} DataType;


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

    // Number of seconds to add to the time that the block is completed to get the reveal time.
    uint32_t ticket_phase_duration;

    // Number of seconds to add to the block state's reveal_time value to get the cutoff period for reveal; any entry
    // which has not been revealed by this point allows zero-penalty ticket return.
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

    // This is a number of seconds to add to the auction start time to get the end of auction time.
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
    // every 1000 lamports of SOL earned via staking, 1 Ki token is awarded.
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

    // These are the seeds of the entries of the block, of which there are config.total_entry_count.  Each seed can be
    // used to derive the address of an entry mint, from which can then be derived the address of an Entry.
    seed_t entry_seeds[];
    
} Block;


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
    // The Entry in either in an auction (if the current time is less than the end of auction time) or is unsold
    // (if the auction end has passed)
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

    // Number of ki to achieve level 1.  Each subsequent level requires twice the Ki to achieve than the previous.
    uint64_t level_1_ki;

    // Metadata for each of the 10 levels
    LevelMetadata level_metadata[10];
    
} EntryMetadata;


typedef struct
{
    // Block containing to this entry
    SolPubkey block_pubkey;

    // Index of this entry within that block
    uint16_t block_index;
    
    // This is identical to the config values in the block that this entry is a part of.  They are duplicated here
    // to allow transactions that otherwise do not need the block, to not have to read it.
    BlockConfiguration block_config;

    // The overall state of the entry
    EntryState state;

    union {
        // Pre entry reveal, this holds the SHA-256 of the following values concatenated together:
        // - The SHA-256 of the Shinobi Systems metadata that will be supplied by the reveal transaction
        // - The SHA-256 of the Metaplex metadata that will be supplied by the reveal transaction
        // - 8 bytes of salt
        // The above is all supplied by the reveal transaction
        sha256_t sha256;
        // Post entry reveal, this holds the account of the entry's token mint
        SolPubkey token_mint;
    };

    // The value present here depends upon the value of entry_state:
    //  EntryState_PreReveal: if a ticket has been sold, this is the ticket mint, else this is all zeroes
    //  EntryState_AwaitingTicketRedeem: the ticket mint
    //  EntryState_InAuction: the winning bid token mint, or zero if there is no bid yet
    //  EntryState_AwaitingClaim: the winning bid token mint
    //  EntryState_Unsold: all zeroes
    //  EntryState_Staked: all zeroes
    SolPubkey ticket_or_bid_mint;

    // If the program holds a stake account for this entry, then this is the pubkey of that stake account
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

    // Total Ki harvested or paid over the lifetime of this entry.  This is what allows levelling up.
    uint64_t total_ki_harvested_or_paid_lamports;

    // Program id of the replacement metadata program which is used to update metadata.  If all zeroes, the nifty
    // program itself updates metadata.  It may be updated only by the owner of the entry, and only to a the "next
    // value" in the sequence of metadata program ids listed in the config.  If this is all zeroes, then the nifty
    // program itself manages the metadata.
    SolPubkey metadata_program;

    // These are the Shinobi metadata values for this entry.  These are all zeroes until the entry is revealed,
    // at which point the verified metadata is copied in.
    EntryMetadata metadata;

    // This is a metadata expansion area, which holds any expansion metadata created by an upgraded metadata program.
    // The entry is allocated with some amount of additional space here, and any replacement metadata program can use
    // read it; it is updated with the results of that metadata program (including re-allocing this Entry if necessary
    // to accomodate the larger size).
    uint8_t metadata_expansion[];
    
} Entry;


// This is the format of data stored in a bid account, which is the PDA associated with a bid token.
typedef struct
{
    // This is an indicator that the data is a Bid
    DataType data_type;

    // This is the account address of the block containing the entry that was bid on
    SolPubkey block_account;

    // This is the index of the entry within the block that was bid on
    uint16_t entry_index;

    // This is the stake account that was supplied as the bid.  This is owned by the nifty program and cannot
    // be operated on in any way until the bid is either exchanged for the token (because it was the winning bid; at
    // which time the stake account becomes subject to all rules of stake accounts for entries in the 'staked' state),
    // or the bid is redeemed as a losing bid (because it was outbid, and the only action possible is the return of
    // the stake account to the owner of the token)
    SolPubkey stake_account;
    
} Bid;


// This is the type of the data stored in the clock sysvar
typedef struct
{
    uint64_t slot;

    timestamp_t epoch_start_timestamp;

    uint64_t epoch;

    uint64_t leader_schedule_epoch;

    timestamp_t unix_timestamp;
} Clock;


// This is the type of data stored in a stake account
typedef enum StakeState
{
    StakeState_Uninitialized =    0,
    StakeState_Initialized  =     1,
    StakeState_Stake =            2,
    StakeState_RewardsPool =      3
} StakeState;


typedef struct __attribute__((packed))
{
    // One of the StakeState enum values
    uint32_t state;

    uint64_t rent_exempt_reserve;

    SolPubkey authorized_staker;

    SolPubkey authorized_withdrawer;

    /// UnixTimestamp at which this stake will allow withdrawal, unless the
    ///   transaction is signed by the custodian
    timestamp_t lockup_unix_timestamp;
    
    /// epoch height at which this stake will allow withdrawal, unless the
    ///   transaction is signed by the custodian
    uint64_t lockup_epoch;
        
    /// custodian signature on a transaction exempts the operation from
    ///  lockup constraints
    SolPubkey lockup_custodian;

    /// to whom the stake is delegated
    SolPubkey voter_pubkey;
    
    /// activated stake amount, set at delegate() time
    uint64_t stake;
        
    /// epoch at which this stake was activated, std::Epoch::MAX if is a bootstrap stake
    uint64_t activation_epoch;
        
    /// epoch the stake was deactivated, std::Epoch::MAX if not deactivated
    uint64_t deactivation_epoch;
    
    /// how much stake we can activate per-epoch as a fraction of currently effective stake
    // This is a 64 bit floating point value
    uint8_t warmup_cooldown_rate[8];
        
    /// credits observed is credits from vote account state when delegated or redeemed
    uint64_t credits_observed;

    /// There are 4 bytes of zero padding stored at the end of stake accounts
    uint8_t padding[4];
        
} StakeAccountData;


// This is the type of data stored in a Solana token program mint account
typedef struct
{
    /// Optional authority used to mint new tokens. The mint authority may only be provided during
    /// mint creation. If no mint authority is present then the mint has a fixed supply and no
    /// further tokens may be minted.
    uint32_t has_mint_authority;
    SolPubkey mint_authority;

    /// Total supply of tokens.
    uint64_t supply;

    /// Number of base 10 digits to the right of the decimal place.
    uint8_t decimals;

    /// Is `true` if this structure has been initialized
    bool is_initialized;

    /// Optional authority to freeze token accounts.
    uint32_t has_freeze_authority;
    SolPubkey freeze_authority;
    
} SolanaTokenProgramMintData;


typedef enum
{
    SolanaTokenAccountState_Uninitialized  = 0,
    SolanaTokenAccountState_Initialized    = 1,
    SolanaTokenAccountState_Frozen         = 2
} SolanaTokenAccountState;

// This is the type of data stored in a Solana token program token account
typedef struct
{
    /// The mint associated with this account
    SolPubkey mint;

    /// The owner of this account.
    SolPubkey owner;

    /// The amount of tokens this account holds.
    uint64_t amount;

    /// If `delegate` is `Some` then `delegated_amount` represents
    /// the amount authorized by the delegate
    uint32_t has_delegate;
    SolPubkey delegate;

    /// The account's state (one of the SolanaTokenAccountState values)
    uint8_t account_state;
        
    /// If has_is_native, this is a native token, and the value logs the rent-exempt reserve. An
    /// Account is required to be rent-exempt, so the value is used by the Processor to ensure that
    /// wrapped SOL accounts do not drop below this threshold.
    uint32_t has_is_native;
    uint64_t is_native;

    /// The amount delegated
    uint64_t delegated_amount;

    /// Optional authority to close the account.
    uint32_t has_close_authority;
    SolPubkey close_authority;
    
} SolanaTokenProgramTokenData;
