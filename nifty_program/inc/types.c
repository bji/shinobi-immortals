
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
