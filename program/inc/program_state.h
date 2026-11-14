#pragma once

#include "data_type.h"


// Shared program state: administrative authority.
typedef struct
{
    // The data type of the data -- DataType_ProgramState
    DataType data_type;

    // The pubkey of the admin user, which is the only user who has rights to execute the admin functions.  This
    // account must sign any transaction requiring admin privileges.
    SolPubkey admin_pubkey;

} ProgramState;
