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

    // Failed to create an account
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

    // Attempted to reveal an entry using an NFT account that does not hash to the correct entry hash
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
    Error_EntryNotInAuction                            = 1029,
    
    // User attempted a bid that was too low
    Error_BidTooLow                                    = 1030,

    // Can't claim bid
    Error_CannotClaimBid                               = 1031,
    
    // Attempt to claim a winning bid as a losing bid
    Error_BidWon                                       = 1032,

    // Can't reclaim bid marker otken
    Error_FailedToReclaimBidMarkerToken                = 1033,

    // Failed to move stake from master stake account out to entry stake account for commission collection purposes
    Error_FailedToMoveStakeOut                         = 1034,
    
    // Failed to move stake from entry stake account to master stake account for commission collection purposes
    Error_FailedToMoveStake                            = 1035,
    
    // Attempt to stake when an entry is not in a stakeable state
    Error_NotStakeable                                 = 1036,
    
    // Attempt to set stake account authorities to the nifty authority failed during a stake operation
    Error_SetStakeAuthoritiesFailed                    = 1037,
    
    // Attempted to harvest Ki from an entry that is not staked
    Error_NotStaked                                    = 1038,
    
    // Attempt to level up an entry that is not owned
    Error_NotOwned                                     = 1039,
    
    // Attempt to level up an entry that is already at max level
    Error_AlreadyAtMaxLevel                            = 1040,
    
    // Deactivate stake failed
    Error_FailedToDeactivate                           = 1041,

//    // The block account doesn't have the correct number of bytes in it
//    Error_InvalidDataLen                               = 1007,
//
//    // Block parameters would result in a block that exceeds maximum allowed system account size
//    Error_BlockTooLarge                                = 1008,
//
//    // Block account was provided but had bad permissions
//    Error_BadPermissions                               = 1009,
//
//    // Attempting an action that is invalid on a completed block
//    Error_BlockAlreadyComplete                         = 1010,
//
//    // Transaction attempted to use a program derived account of the wrong type
//    Error_IncorrectAccountType                         = 1012,
//
//    // Attempted to reveal an NFT that was not owned by the nifty program
//    Error_InvalidNFTOwner                              = 1014,
//
//    // Attempted to reveal an account that wasn't even a valid NFT
//    Error_InvalidNFTAccount                            = 1015,
//
//    // Suppled stake account was not a valid stake account
//    Error_InvalidStakeAccount                          = 1019,
//
//    // Add entries finished adding all entries for a block but did not supply a link block
//    Error_MissingLinkBlock                             = 1020,
//
//    // Insufficient funds provided for operation
//    Error_InsufficientFunds                            = 1026,
//
//    // Invalid metaplex metadata in metadata account
    Error_InvalidMetadataValues                        = 1027,
//
//    // Attempt to claim when a claim is not possible
//    Error_ClaimNotPossible                             = 1031,
//    
//    // Delegate stake failed
    Error_FailedToDelegate                             = 1032,
//
//
//    // Attempt to set stake account authorities to the nifty authority failed during a stake operation
//    Error_SetStakeAuthoritiesFailed                    = 1034,
//
//    // Attempt to charge commission when there is no commission to charge
//    Error_NoCommissionToCharge                         = 1036,
//
    
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
