
#ifndef PROGRAM_CONFIG_H
#define PROGRAM_CONFIG_H

#include "data_type.h"


// This is the type of data stored in the 
typedef struct
{
    // The data type of the data -- DataType_ProgramConfig
    DataType data_type;

    // The pubkey of the admin user, which is the only user who has rights to execute the admin functions.  This
    // account must sign any transaction requiring admin privileges.
    SolPubkey admin_pubkey;

    // The number of metadata program ids in the metadata program id list
    uint32_t metadata_program_id_count;

    // The metadata program ids.  As the metadata program is upgraded, a new program id is written to the end of this
    // list.  Any block entry may be upgraded to the next metadata program id in sequence in this list, which will
    // cause that metadata program to then become responsible for managing the metadata of the entry.
    SolPubkey metadata_program_id[];
} ProgramConfig;


#endif // PROGRAM_CONFIG_H
