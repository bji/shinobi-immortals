
// This is all error codes that can be returned by this program
typedef enum Error
{
    // Returned when the instruction data is malformed and cannot be deserialized.  This should never happen as it
    // would imply that the validator provided malformed data structures to the program.
    Error_InvalidData                    = 1000,
    
    // Returned when the data that is supplied in the instruction is less than required
    Error_ShortData                      = 1001,
    
    // Returned when the instruction code that is in the first byte of the instruction data does not represent a valid
    // instruction
    Error_UnknownInstruction             = 1002,
    
    // An admin function was invoked by a transaction that was not signed by the admin user
    Error_PermissionDenied               = 1003,
    
    // The seeds provided in the instruction data could not be used to compute a valid Program Derived Address
    Error_InvalidBlockAddressSeeds       = 1004,
    
    // Failed to create an account
    Error_CreateAccountFailed            = 1005,
    
    // The transaction specified an incorrect number of accounts for the instruction
    Error_IncorrectNumberOfAccounts      = 1006,

    // The funding account used had invalid properties
    Error_InvalidFundingAccount          = 1007,
    
    // The System program account listed was not the system program id
    Error_InvalidSystemAccount           = 1008,
    
    // The block account that was supplied did not match that derived from the seeds that were supplied
    Error_InvalidBlockAccount            = 1009,
    
    // The block account doesn't have the correct number of bytes in it
    Error_InvalidDataLen                 = 1010,

    // Block parameters would result in a block that exceeds maximum allowed system account size
    Error_BlockTooLarge                  = 1011,

    // Account provided as a destination for lamports was not writable
    Error_InvalidDestinationAccount      = 1012,

    // Block account was provided but had bad permissions
    Error_BadPermissions                 = 1013,

    // Could not delete a block because it was complete already
    Error_CompletedBlockCannotBeDeleted  = 1014,
    
    // Errors 1100 through 1199 are used to indicate an error in input data, where the specific input field
    // that was faulty is the offset from 1100.
    Error_InvalidInputData_First         = 1100,
    Error_InvalidInputData_Last          = 1199
} Error;
