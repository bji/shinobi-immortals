#pragma once

// This is all error codes that can be returned by this program
typedef enum
{
    // Returned when the instruction data is malformed and cannot be deserialized.  This should never happen as it
    // would imply that the validator provided malformed data structures to the program.
    Error_InvalidData                                  = 1000,
    
    // Returned when the data that is supplied in the instruction is less than or more than required
    Error_InvalidDataSize                              = 1001,
    
    // Returned when the instruction code that is in the first byte of the instruction data does not represent a valid
    // instruction
    Error_UnknownInstruction                           = 1002,
    
    // An admin function was invoked by a transaction that was not signed by the admin user
    Error_PermissionDenied                             = 1003,
    
    // The seeds provided in the instruction data could not be used to compute a valid Program Derived Address
    Error_InvalidBlockAddressSeeds                     = 1004,
    
    // Failed to create an account
    Error_CreateAccountFailed                          = 1005,
    
    // The transaction specified an incorrect number of accounts for the instruction
    Error_IncorrectNumberOfAccounts                    = 1006,

    // The block account doesn't have the correct number of bytes in it
    Error_InvalidDataLen                               = 1007,

    // Block parameters would result in a block that exceeds maximum allowed system account size
    Error_BlockTooLarge                                = 1008,

    // Block account was provided but had bad permissions
    Error_BadPermissions                               = 1009,

    // Attempting an action that is invalid on a completed block
    Error_BlockAlreadyComplete                         = 1010,

    // Failed to fetch the clock sysvar
    Error_FailedToGetClock                             = 1011,

    // Transaction attempted to use a program derived account of the wrong type
    Error_IncorrectAccountType                         = 1012,

    // Attempt to reveal an entry which was already revealed
    Error_AlreadyRevealed                              = 1013,

    // Attempted to reveal an NFT that was not owned by the nifty program
    Error_InvalidNFTOwner                              = 1014,

    // Attempted to reveal an account that wasn't even a valid NFT
    Error_InvalidNFTAccount                            = 1015,

    // Attempted to reveal an entry using an NFT account that does not hash to the correct entry hash
    Error_InvalidHash                                  = 1016,

    // Attempted an action that requires a completed block, but the block was not complete
    Error_BlockNotComplete                             = 1017,

    // Attempt to purchase an entry which has already been purchased
    Error_AlreadyPurchased                             = 1018,

    // Suppled stake account was not a valid stake account
    Error_InvalidStakeAccount                          = 1019,

    // Add entries finished adding all entries for a block but did not supply a link block
    Error_MissingLinkBlock                             = 1020,

    // Attempt to perform an operation that requires a block to have met its reveal criteria, on a block which hasn't
    Error_BlockNotRevealable                           = 1021,

    // Attempt to perform an operation that requires a revealable Entry, for an Entry which is not revealable
    Error_EntryNotRevealable                           = 1022,
    
    // Attempt to buy an entry in a block that is beyond its mystery phase, but the entry has not been revealed yet
    Error_EntryWaitingForReveal                        = 1023,

    // Attempt to buy an entry during an auction
    Error_EntryInAuction                               = 1024,

    // Attempt to buy an entry which was already won in auction
    Error_EntryWaitingToBeClaimed                      = 1025,

    // Insufficient funds provided for operation
    Error_InsufficientFunds                            = 1026,

    // Invalid metaplex metadata in metadata account
    Error_InvalidMetadataValues                        = 1027,

    // Attempt to bid on an entry which is not in auction
    Error_EntryNotInAuction                            = 1028,

    // User attempted a bid that was too low
    Error_BidTooLow                                    = 1029,

    // Attempt to refund an entry that was already refunded
    Error_AlreadyRefunded                              = 1030,

    // Attempt to claim when a claim is not possible
    Error_ClaimNotPossible                             = 1031,
    
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
    Error_InvalidData_Last                             = 1399
} Error;
