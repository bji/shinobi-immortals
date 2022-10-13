#pragma once

#include "data_type.h"


// This is the type of data stored in the
typedef struct
{
    // The data type of the data -- DataType_ProgramConfig
    DataType data_type;

    // The pubkey of the admin user, which is the only user who has rights to execute the admin functions.  This
    // account must sign any transaction requiring admin privileges.
    SolPubkey admin_pubkey;

} ProgramConfig;
