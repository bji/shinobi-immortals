#pragma once


// Enum which defines the types of data that may be stored in PDA accounts.  This value is the first value in
// every account and allows a sanity check on the type of data in the account.
typedef enum
{
    // This type is never used for valid data, because it's the same value that would be present in an uninitialized
    // account
    DataType_Invalid        = 0,

    // Program config
    DataType_ProgramConfig  = 1,
    
    // Block
    DataType_Block          = 2,

    // Entry
    DataType_Entry          = 3

} DataType;
