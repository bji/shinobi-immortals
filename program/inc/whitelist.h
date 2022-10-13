#pragma once

// This is the maximum number of whitelist entries allowed in a whitelist.  The absolute maximum that would fit into
// the 10KB limit of a PDA is 319.
#define MAX_WHITELIST_ENTRIES 300

// This is the format of data stored in a whitelist account
typedef struct
{
    // This is an indicator that the data is a Whitelist
    DataType data_type;

    // Number of entries in the whitelist.  Entries may be removed after being added, in which case the entry
    // at that index will be all zeroes.
    uint16_t count;

    // Pubkeys of system accounts that are whitelisted for a block.
    SolPubkey entries[MAX_WHITELIST_ENTRIES];
}
Whitelist;
