

// This is a timestamp.  If this is a positive value, it is a number of seconds since the Unix Epoch, as presented
// by the Clock sysvar.  If this is a negative value, then its absolute value is a slot height as presented by
// the SlotHistory sysvar.
typedef int64_t timestamp_t;


// This is the format of data stored in the program config account
typedef struct
{
    // Pubkey of the admin user, which is the only user allowed to perform admin functions
    SolPubkey admin_pubkey;

    // This is the payout pubkey, where all funds paid to the operator of the nifty_stakes program are deposited
    SolPubkey payee_pubkey;
} ProgramConfig;


typedef struct
{
    // The Program Derived Account address is generated from two 32 bit numbers.  The first is a block group id.  The
    // second is a block id within that group.  These two numbers together (in their little endian encoding) form the
    // seed that is used to generate the address of the block (using whatever bump seed first computes a valid PDA).
    // If the PDA that is derived from these seeds is invalid, the transaction fails.
    uint32_t group_id;
    uint32_t block_id;
    
    // Total number of entries in this block.  Must be greater than 0.
    uint16_t total_entry_count;

    // Number of tickets which when purchased will allow a reveal.  Must be equal to or less than total_entry_count.
    uint16_t reveal_ticket_count;

    // This is either a number of seconds to add to the current slot/time to get the reveal slot/time.
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

    // This is the commission to charge on stake reward earned by stake accounts.  It is a binary fractional value,
    // with 0xFFFF representing 100% commission and 0x0000 representing 0% commission.
    uint16_t commission;

    // This is the minimum percentage of lamports commission to charge for an NFT.  Which means that when an NFT is
    // returned, if the total commission charged while the user owned the NFT is less than minimum_accrued_commission,
    // then commission will be charged to make up the difference.  Like other commission values, it is a binary
    // fractional value, with 0xFFFF representing 100% commission and 0x0000 representing 0% commission.
    uint16_t minimum_accrued_commission;

    // This is the fraction of the stake account balance to use as the "permabuy" price.  If the user has a purchased
    // an entry, they can get their stake account back while retaining the entry by paying this "permabuy" commission.
    // Then the entry is basically owned outright by the user without any stake account committment.  It is a binary
    // fractional value, with 0xFFFF representing 100% commission and 0x0000 representing 0% commission.  Must be
    // nonzero.
    uint16_t permabuy_commission;

    // This is the extra cost in lamports charged when a stake account that is not delegated to Shinobi Systems
    // is un-delegated via the redelegation crank.
    uint64_t undelegate_charge_lamports;

    // This is the extra cost in lamports charged when a stake account that is undelegated is delegated to
    // Shinobi Systems via the redelegation crank.
    uint64_t delegate_charge_lamports;

    // This is the number of stake earned lamports per Ki that is earned.  For example, 1000 would mean
    // that for every 1000 lamports of SOL earned via staking, 1 Ki token is awarded.
    uint32_t ki_factor;

    // Fraction of Ki that can be replaced by SOL when levelling up.  For example, if this were 0.5, and if the total
    // amount of Ki needed to go from level 2 to level 3 were 1,000 Ki; and if the total Ki earnings were only 500,
    // then 500 Ki worth of SOL could be paid as an alternate cost for completing the level up.  This value is a
    // fraction with 0xFFFF representing 1.0 and 0x0000 representing 0.  If this value is 0xFFFF, then an owner of the
    // NFT can just directly pay SOL to level up regardless of cumulative Ki earnings.  If this value is 0, then the
    // NFT cannot be leveled up except by fulling earning all required Ki.
    uint16_t sol_level_up_fraction;

    // This is the number of bytes in mint_data for each BlockEntry in this block
    uint16_t mint_data_size;

} BlockConfiguration;


typedef struct
{
    // This is the total number of entries that have been added to the block thus far.
    uint16_t added_entries_count;

    // This is the timestamp of the end of the reveal grace period.  This value is set when the last entry is added
    // to the block as the timestamp at which the last entry was added, plus ticket_phase_duration, plus
    // reveal_grace_duration.  It is updated when the number of tickets sold becomes equal to reveal_ticket_count
    // (which may be as soon as the last entry is added, if reveal_ticket_count is 0) to that timestamp plus
    // reveal_grace_duration.
    timestamp_t reveal_grace_end_timestamp;

} BlockState;


typedef enum
{
    // Entry is 
    BlockEntryState_UnsoldTicket          = 0,
    BlockEntryState_PurchasedTicket       = 1,
    BlockEntryState_Owned                 = 2,
    BlockEntryState_InAuction             = 3,
    BlockEntryState_Unsold                = 4,
    BlockEntryState_PermanentlyOwned      = 5
    
} BlockEntryState;


typedef struct __attribute__((__packed__))
{
    // The current state of the Entry
    BlockEntryState entry_state;

    // The value present here depends upon the value of entry_state:
    //  BlockEntryState_UnsoldTicket: undefined
    //  BlockEntryState_PurchasedTicket: the ticket token pubkey
    //  BlockEntryState_Owned: the NFT token pubkey
    //  BlockEntryState_InAuction: the NFT token pubkey, or all zero if the NFT was not minted yet
    //  BlockEntryState_Unsold: the NFT token pubkey, or all zero if the NFT was not minted yet
    //  BlockEntryState_PermanentlyOwned: the NFT token pubkey
    SolPubkey entry_pubkey;

    // This union holds additional values depending on BlockEntryState.  A union is used because keeping BlockEntry
    // size down is critically important for keeping account size as low as possible.
    union {
        // If entry_state is BlockEntryState_Owned, this struct is used
        struct {
            // If this BlockEntry is BlockEntryState_Owned, this is the address of the stake account that is held for
            // this entry.
            SolPubkey stake_account_pubkey;
            
            // Number of lamports in the stake account at the time of its last commission collection
            uint64_t last_commission_charge_stake_account_lamports;
            
            // Total cumulative commection collected in lamports
            uint64_t total_commission_charged_lamports;
        } owned;
        
        // If entry_state is BlockEntryState_InAuction, this struct is used
        struct {
            // The auction begin time, if this entry is in auction, otherwise zero
            timestamp_t auction_begin_timestamp;
            
            // The current maximum auction bid for this entry, if it is in auction.  0 if no bids have been received
            // or it's not in auction.
            uint64_t maximum_bid_lamports;
            
            // The current winning auction bid token, if there is a winning bid.
            SolPubkey winning_bidder;
        } in_auction;
    };
    
    // Total Ki harvested over the lifetime of this entry
    uint64_t total_ki_harvested_lamports;
    
    // This is the account address of the mint program which manages this entry.  This is set when the entry is first
    // created, from the ProgramConfig.metadata_program_id value.  At any time, the ProgramConfig may be updated to a
    // new program.  But the metadata program actually used for this block is not updated unless the owner of the
    // entry issues a valid update_metadata instruction (if the entry is not owned, then the admin user can issue this
    // instruction).  In this way, owned entries cannot have their metadata program changed without consent of the
    // owner.
    SolPubkey mint_program_id;
    
    // This is the SHA-256 hash of the mint data.  It is always published in the entry and allows program to ensure
    // that only the values contents that were computed before the reveal are presented after the reveal.
    uint8_t mint_data_hash[32];

    // This is the mint data itself.  Its contents prior to reveal are all zeroes.  Post-reveal, the contents and
    // their meaning is determined by the mint program.  The actual number of bytes present in the mint_data of each
    // entry is determined by the block's [mint_data_size].
    uint8_t mint_data[0];
    
} BlockEntry;


// This is the format of block data stored in a block
typedef struct
{
    // This is the configuration of the block.  It is never changed after the block is created.
    BlockConfiguration config;

    // These are the state values of the block.
    BlockState state;

    // The block will have been created with [total_entry_count] entries in place of this empty array
    BlockEntry entries[0];
    
} BlockData;


// This macro evaluates to the address of a BlockEntry within the entries array
#define BLOCK_ENTRY(block_data, index)                                                       \
    ((BlockEntry *)                                                                          \
     (((uint8_t *) ((block_data)->entries)) +                                                \
      (((sizeof(BlockEntry) + (block_data)->config.mint_data_size + 3) % 4) * (index))))
