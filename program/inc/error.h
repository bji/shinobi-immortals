#pragma once

// This is all error codes that can be returned by this program
typedef enum
{
    // Returned when the instruction data is malformed and cannot be deserialized.  This should never happen as it
    // would imply that the validator provided malformed data structures to the program.
    Error_InvalidData                                  = 1000,

    // Returned when the data that is supplied in the instruction is less than or more than required
    Error_InvalidDataSize                              = 1001,

    // The transaction specified an incorrect number of accounts for the instruction
    Error_IncorrectNumberOfAccounts                    = 1002,

    // Returned when the instruction code that is in the first byte of the instruction data does not represent a valid
    // instruction
    Error_UnknownInstruction                           = 1003,

    // An admin function was invoked by a transaction that was not signed by the admin user
    Error_PermissionDenied                             = 1004,

    // The address of an account to be created is not the expected program derived address
    Error_CreateAccountFailed                          = 1005,

    // Failed to fetch the clock sysvar
    Error_FailedToGetClock                             = 1006,

    // Attempted an action that requires a completed block, but the block was not complete
    Error_BlockNotComplete                             = 1007,

    // Attempt to perform an operation that requires a block to have met its reveal criteria, on a block which hasn't
    Error_BlockNotRevealable                           = 1008,

    // Attempt to reveal an entry which was already revealed
    Error_AlreadyRevealed                              = 1009,

    // The mint account that was passed in was not correct
    Error_InvalidMintAccount                           = 1010,

    // The token account that was passed in was not correct
    Error_InvalidTokenAccount                          = 1011,

    // The metaplex metadata account that was passed in was not correct
    Error_InvalidMetadataAccount                       = 1012,

    // Attempt to perform an operation that requires a revealable Entry, for an Entry which is not revealable
    Error_EntryNotRevealable                           = 1013,

    // Attempted to reveal an entry using a token account that does not hash to the correct entry hash
    Error_InvalidHash                                  = 1014,

    // Attempt to purchase an entry which is already owned
    Error_AlreadyOwned                                 = 1015,

    // Attempt to buy an entry in a block that is beyond its mystery phase, but the entry has not been revealed yet
    Error_EntryWaitingForReveal                        = 1016,

    // Attempt to buy an entry during an auction
    Error_EntryInAuction                               = 1017,

    // Attempt to buy an entry which was already won in auction
    Error_EntryWaitingToBeClaimed                      = 1018,

    // Insufficient funds provided for requested operation
    Error_InsufficientFunds                            = 1019,

    // Attempt to refund an entry that was already refunded
    Error_AlreadyRefunded                              = 1020,

    // Attempt to bid on an entry which is not in auction
    Error_EntryNotInAuction                            = 1021,

    // User attempted a bid that was too low
    Error_BidTooLow                                    = 1022,

    // Can't claim bid
    Error_CannotClaimBid                               = 1023,

    // Attempt to claim a winning bid as a losing bid
    Error_BidWon                                       = 1024,

    // Can't reclaim bid marker otken
    Error_FailedToReclaimBidMarkerToken                = 1025,

    // Attempt to stake when an entry is not in a stakeable state
    Error_NotStakeable                                 = 1026,

    // Attempted an operation that requires a staked entry (Destake, Harvest, Delegate, stake-only ReAuthorize)
    // on an entry that is not staked
    Error_NotStaked                                    = 1027,

    // Attempt to level up, or thaw the token account of, an entry that is not owned
    Error_NotOwned                                     = 1028,

    // Attempt to level up an entry that is already at max level
    Error_AlreadyAtMaxLevel                            = 1029,

    // Invalid metaplex metadata in metadata account
    Error_InvalidMetadataValues                        = 1030,

    // User requested a buy but privided a maximum allowed price that is lower than the actual price
    Error_PriceTooHigh                                 = 1031,

    // Not the correct whitelist account
    Error_NotWhitelistAccount                          = 1032,

    // Cannot add entries to a whitelist when the block is already created
    Error_BlockAlreadyExists                           = 1033,

    // Attempted to add more whitelist entries than are supported by a whitelist
    Error_TooManyWhitelistEntries                      = 1034,

    // Cannot delete a whitelist for a block in progress
    Error_WhitelistBlockInProgress                     = 1035,

    // Buy attempt failed because the fee payer was not in the whitelist
    Error_FailedWhitelistCheck                         = 1036,

    // Attempt to add entries to an already complete block
    Error_BlockAlreadyComplete                         = 1037,

    // Invalid attempt to resize an account
    Error_InvalidResize                                = 1038,

    // Attempt to level up with an amount of Ki that would not earn even one tenth of a level credit; no Ki is burned
    Error_NoLevelCreditsEarned                         = 1039,

    // Attempt to delegate an entry's stake account when it is already actively delegated
    Error_AlreadyDelegated                             = 1040,

    // Attempt to withdraw excess lamports from an account that holds nothing beyond what it is required to hold
    Error_NothingToWithdraw                            = 1041,

    // Attempt by someone other than the winning bidder to settle a winning bid before
    // SETTLE_WINNING_BID_DELAY_SECONDS have passed since the end of the auction
    Error_OnlyBidderCanSettleYet                       = 1042,

    // Attempt to thaw the token account of an entry that is staked; it stays frozen for as long as the entry is staked
    Error_EntryStaked                                  = 1043,

    // Attempt to thaw a token account that is not frozen
    Error_TokenAccountNotFrozen                        = 1044,

    // Attempt to create a block that uses a whitelist (whitelist_duration > 0) when its whitelist does not exist, or
    // is empty; a block's whitelist must be created, with its entries, before the block
    Error_WhitelistRequired                            = 1045,

    // MeasureRewardRate was asked to fail if the reward rate had not been measured in the current level credit window,
    // and it had not been, and could not be measured then
    Error_RewardRateNotMeasured                        = 1046,

    // Errors Error_InvalidAccount_First through Error_InvalidAccount_Last are used to indicate an error in input
    // account, where the specific input field that was faulty is the offset from Error_InvalidAccount_First
    Error_InvalidAccount_First                         = 1100,
    Error_InvalidAccount_Last                          = 1199,

    // Errors Error_InvalidAccountPermissions_First through Error_InvalidAccountPermissions_Last are used to indicate
    // an error in input account, where the specific input field that was faulty is the offset from
    // Error_InvalidAccountPermissions_First
    Error_InvalidAccountPermissions_First              = 1200,
    Error_InvalidAccountPermissions_Last               = 1299,

    // Errors Error_InvalidData_First through Error_InvalidData_Last are used to indicate an error in input data,
    // where the specific input field that was faulty is the offset from Error_InvalidData_First
    Error_InvalidData_First                            = 1300,
    Error_InvalidData_Last                             = 1399,

    // Internal programming error
    Error_InternalProgrammingError                     = 2000
} Error;
