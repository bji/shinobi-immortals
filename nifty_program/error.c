
// This is all error codes that can be returned by this program
typedef enum Error
{
    // Returned when the instruction data is malformed and cannot be deserialized.  This should never happen as it
    // would imply that the validator provided malformed data structures to the program.
    Error_InvalidData                  = 1000,
    // Returned when the data that is supplied in the instruction is less than required
    Error_ShortData                    = 1001,
    // Returned when the instruction code that is in the first byte of the instruction data does not represent a valid
    // instruction
    Error_UnknownInstruction           = 1002,
    // An admin function was invoked by a transaction that was not signed by the admin user
    Error_PermissionDenied             = 1003,
    // The seeds provided in the instruction data could not be used to compute a valid Program Derived Address
    Error_InvalidBlockAddressSeeds     = 1004,
    // Failed to create an account
    Error_CreateAccountFailed          = 1005,
    // The transaction specified an incorrect number of accounts for the instruction
    Error_IncorrectNumberOfAccounts    = 1006,
    // The funding account used had invalid properties
    Error_InvalidFundingAccount        = 1007,
    // The System program account listed was not the system program id
    Error_InvalidSystemAccount         = 1008
} Error;
