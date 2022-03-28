

// This is a timestamp.  If this is a positive value, it is a number of seconds since the Unix Epoch, as presented
// by the Clock sysvar.  If this is a negative value, then its absolute value is a slot height as presented by
// the SlotHistory sysvar.
typedef int64_t timestamp_t;

// This is a commission.  It is a binary fractional value, with 0xFFFF representing 100% commission and 0x0000
// representing 0% commission.
typedef uint16_t commission_t;

typedef uint8_t sha256_t[32];


// This is the format of data stored in the program config account
typedef struct
{
    // Pubkey of the admin user, which is the only user allowed to perform admin functions
    SolPubkey admin_pubkey;

    // This is the payout pubkey, where all funds paid to the operator of the nifty_stakes program are deposited
    SolPubkey payee_pubkey;
} ProgramConfig;


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

    // Number of tickets which when purchased will allow a reveal.  Must be equal to or less than total_entry_count.
    uint16_t reveal_ticket_count;

    // This is the number of seconds to add to the time that the block is completed to get the reveal time.
    uint32_t ticket_phase_duration;

    // This is a number of seconds to add to the block state's reveal_time value to get the cutoff period for reveal;
    // any entry which has not been revealed by this point allows zero-penalty ticket return.
    uint32_t reveal_grace_duration;

    // This is cost in lamports of each ticket.  Cannot be zero.
    uint64_t ticket_price_lamports;

    // This is the number of lamports to return to the user when a ticket is refunded.  It must be less than or equal
    // to ticket_price_lamports.  Tickets that are refunded after reveal grace period (which is the block state
    // reveal_time plus reveal_grace_seconds) are refunded full lamports instead of ticket_refund_lamports.  In this
    // way, ticket owners can be assured that if the reveal does not occur in a timely fashion, they can get 100% of
    // their funds back.
    uint64_t ticket_refund_lamports;

    // This is either a number of seconds to add to the current time to get the end of auction time.
    uint32_t auction_duration;

    // This is the minimum number of lamports for any auction bid.  This is also the sale price of an entry whose
    // auction has completed without any bids.  Must be nonzero.
    uint64_t bid_minimum;

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
    // then 500 Ki worth of SOL could be paid as an alternate cost for completing the level up.  This value is a
    // fraction with 0xFFFF representing 1.0 and 0x0000 representing 0.  If this value is 0xFFFF, then an owner of the
    // NFT can just directly pay SOL to level up regardless of cumulative Ki earnings.  If this value is 0, then the
    // NFT cannot be leveled up except by fulling earning all required Ki.
    uint16_t level_up_sol_payable_fraction;

} BlockConfiguration;


typedef struct
{
    // This is the total number of entries that have been added to the block thus far.
    uint16_t added_entries_count;

    // This is the timestamp that the last entry was added to the block and it became complete; at that instant,
    // the block is complete and the ticket phase begins.
    timestamp_t block_start_time;

    // Commission currently charged per epoch.  It is a binary fractional value, with 0xFFFF representing 100%
    // commission and 0x0000 representing 0% commission.  This value can be updated but not more often than once
    // per epoch, and if the updated commission is more than 5%, then it can only be updated in maximum increments
    // of 2%.  In addition, commission cannot be changed if any entry has a last_commission_charge_epoch that is
    // not the current epoch (i.e. all accounts must be up-to-date with commission charges).
    commission_t commission;

    // Epoch of the last time that the commission was changed
    uint64_t last_commission_change_epoch;

} BlockState;


// All possible states of an entry
typedef enum
{
    // Entry has not been revealed yet.  The only allowed actions are:
    // - Buy ticket
    // - Refund ticket
    // - Reveal
    BlockEntryState_PreReveal             = 0,
    // Entry has been revealed, and a ticket was purchased for it, but the ticket has not been redeemed yet.  The only
    // allowed action is:
    // - Redeem ticket
    BlockEntryState_AwaitingTicketRedeem  = 1,
    // The Entry in either in an auction (if the current time is less than the end of auction time) or is unsold
    // (if the auction end has passed)
    BlockEntryState_Unsold                = 2,
    // The entry is owned and staked
    BlockEntryState_Staked                = 3
} BlockEntryState;


// Packed to save space, since each entry needs to be as small as possible to allow the largest number possible
// to fit in a block
typedef struct __attribute__((packed))
{
    BlockEntryState state;
    
    union {
        // Prior to entry reveal, this holds the SHA-256 of the following values concatenated together:
        // - SHA-256 of the token id
        // - SHA-256 of the metaplex on-chain metadata
        // - SHA-256 of the shinobi systems on-chain metadata
        // - 8 bytes of salt (provided in the reveal transaction)
        sha256_t entry_sha256;
        // Post entry reveal, this holds the account of the NFT
        SolPubkey token_account;
    };

    // The value present here depends upon the value of entry_state:
    //  BlockEntryState_PreReveal: if a ticket has been sold, this is the ticket NFT, else this is all zeroes
    //  BlockEntryState_AwaitingTicketRedeem: the ticket NFT
    //  BlockEntryState_InAuction: the winning bid NFT, or zero if there is no bid yet
    //  BlockEntryState_AwaitingClaim: the winning bid NFT
    //  BlockEntryState_Unsold: all zeroes
    //  BlockEntryState_Staked: all zeroes
    SolPubkey ticket_or_bid_account;

    // If the program holds a stake account for this entry, then this is the pubkey of that stake account
    SolPubkey stake_account;

    // This union holds additional values depending on BlockEntryState.  A union is used because keeping BlockEntry
    // size down is critically important for keeping account size as low as possible.
    union {
        // If entry_state is BlockEntryState_Unsold, this struct is used
        struct {
            // The auction begin time
            timestamp_t auction_begin_timestamp;
            
            // The current maximum auction bid for this entry, or 0 if no bids have been received
            uint64_t maximum_bid_lamports;
        } unsold;

        // If entry_state is BlockEntryState_Staked, this struct is used
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
    
} BlockEntry;


// This is the format of data stored in a block account
typedef struct
{
    // This is an indicator that the data is a Block
    DataType data_type;
    
    // This is the configuration of the block.  It is never changed after the block is created.
    BlockConfiguration config;

    // These are the state values of the block.
    BlockState state;

    // The block will have been created with [total_entry_count] entries in place of this empty array
    BlockEntry entries[0];
    
} Block;


// This is the format of data stored in a bid account, which is the PDA associated with a bid token
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
