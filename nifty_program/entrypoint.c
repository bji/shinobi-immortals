
// Include solana SDK
#include "solana_sdk.h"

// Include constants
#include "inc/constants.h"

// Include definition of all errors
#include "inc/error.h"


// These are all instructions that this program can execute
typedef enum
{
    // Super user functions: only the super user may perform these actions ---------------------------------------------
    // Initialize configuration
    Instruction_Initialize                    =  0,
    // Set admin
    Instruction_SetAdmin                      =  1,

    // Admin functions: only the admin may perform these actions -------------------------------------------------------
    // Create a new block
    Instruction_CreateBlock                   =  2,
    // Add entries to a newly created block.  Is called multiple times in sequence until the block has had all of its
    // entries added.  When the last entry is added, the block becomes live
    Instruction_AddEntriesToBlock             =  3,
    // Set bytes of metadata in an entry.  This can only be done after the entry's block has reached its reveal
    // criteria.
    Instruction_SetMetadataBytes              =  4,
    // Reveal entries that have not been revealed yet.  This can only be done after the block has reached its reveal
    // criteria, and the entries have had their metadata set.
    Instruction_RevealEntries                 =  5,
    // Update the commission charged per epoch per stake account.  This is in addition to the validator commission.
    // This command fails if the commission has already been updated in the current epoch, and also if the new
    // commission is more than 2% higher than the old commission.
    Instruction_SetBlockCommission            =  6,
    // Split stake off of the master stake account
    Instruction_SplitMasterStake              =  7,

    // User functions: end users may perform these actions -------------------------------------------------------------
    // Buy, either during mystery period, or after end of auction, or during a "reverse" auction
    Instruction_Buy                           =  8,
    // Request a refund of an entry that was purchased before reveal, and was not revealed before the reveal grace
    // period completed
    Instruction_Refund                        =  9,
    // Bid on an entry that is in normal auction
    Instruction_Bid                           = 10,
    // Claim a losing bid
    Instruction_ClaimLosing                   = 11,
    // Claim a winning bid
    Instruction_ClaimWinning                  = 12,
    
    // Stake the entry, providing a stake account to stake the entry to.  The stake account becomes owned by the
    // progrmam.
    Instruction_Stake                         = 13,
    // Destake the entry, returning the stake account of the entry.
    Instruction_Destake                       = 14,
    // Harvest Ki
    Instruction_Harvest                       = 15,
    // Level up an entry.  This requires as input am amount of Ki, which is burned.
    Instruction_LevelUp                       = 16,

    // Anyone functions: anyone may perform these actions --------------------------------------------------------------
    // If the entry is staked and the stake account is delegated, this pays any commission owed to the admin account.
    // If the entry is staked and the stake account is not delegated, this delgates the stake account to Shinobi
    // Systems so that it can start earning Ki
    Instruction_TakeCommissionOrDelegate      = 17,

    // Special functions -----------------------------------------------------------------------------------------------
    // This is a special function that can only be called on an entry that is in the Owned or OwnedAndStaked state.
    // The transaction must be signed by both the admin and the user who owns the entry.  This instruction will reset
    // the metadata update authority to a new authority, and if the entry is staked, will also set the stake account
    // stake and withdraw authorities to the new authority.  The goal of this is to allow a safety valve in case of
    // bugs or problems with this program; or a need to upgrade the program to handle new conditions.  The program
    // can't be upgraded but a new program can be made and then given authority over all user owned entries via this
    // instruction (but only if both the user and admin agree to do so).
    Instruction_ReAuthorize                   = 18
    
} Instruction;


// The actual instruction processing functions have been split up into separate files for ease of use
#include "super/super_initialize.c"
#include "super/super_set_admin.c"

#include "admin/admin_create_block.c"
#include "admin/admin_add_entries_to_block.c"
#include "admin/admin_set_metadata_bytes.c"
#include "admin/admin_reveal_entries.c"
//#include "admin/admin_set_block_commission.c"
//#include "admin/admin_split_master_stake.c"

#include "user/user_buy.c"
#include "user/user_refund.c"
#include "user/user_bid.c"
#include "user/user_claim_losing.c"
#include "user/user_claim_winning.c"
#include "user/user_stake.c"
#include "user/user_destake.c"
#include "user/user_harvest.c"
#include "user/user_level_up.c"

#include "anyone/anyone_take_commission_or_delegate.c"

// #include "special/special_reauthorize.c"


// Program entrypoint
uint64_t entrypoint(const uint8_t *input)
{
    SolParameters params;

    // At most 21 accounts are supported for any command.  This is enough for 3 entries in the "add entries to block"
    // instruction.
    SolAccountInfo account_info[21];
    params.ka = account_info;
    
    // Deserialize parameters.  Must succeed.
    if (!sol_deserialize(input, &params, sizeof(account_info) / sizeof(account_info[0]))) {
        return Error_InvalidData;
    }

    // If there isn't even an instruction index, the instruction is invalid.
    if (params.data_len < 1) {
        return Error_InvalidDataSize;
    }

    // The instruction index is the first byte of data.  For each instruction code, call the appropriate do_ function
    // to handle that instruction, and return its result.
    switch (params.data[0]) {
    case Instruction_Initialize:
        return super_initialize(&params);
        
    case Instruction_SetAdmin:
        return super_set_admin(&params);
        
    case Instruction_CreateBlock:
        return admin_create_block(&params);

    case Instruction_AddEntriesToBlock:
        return admin_add_entries_to_block(&params);

    case Instruction_SetMetadataBytes:
        return admin_set_metadata_bytes(&params);

    case Instruction_RevealEntries:
        return admin_reveal_entries(&params);

////    case Instruction_SetBlockCommission:
////        return admin_set_block_commission(&params);

    case Instruction_Buy:
        return user_buy(&params);

    case Instruction_Refund:
        return user_refund(&params);

    case Instruction_Bid:
        return user_bid(&params);

    case Instruction_ClaimLosing:
        return user_claim_losing(&params);

    case Instruction_ClaimWinning:
        return user_claim_winning(&params);

    case Instruction_Stake:
        return user_stake(&params);

    case Instruction_Destake:
        return user_destake(&params);

    case Instruction_Harvest:
        return user_harvest(&params);

    case Instruction_LevelUp:
        return user_level_up(&params);

    case Instruction_TakeCommissionOrDelegate:
        return anyone_take_commission_or_delegate(&params);

//    case Instruction_ReAuthorize:
//        return special_reauthorize(&params);

    default:
        return Error_UnknownInstruction;
    }
}


// Provide a memcpy implementation; this allows structure assignments that the compiler will turn into
// memcpy which is safer that calling memcpy directly
void memcpy(void *dst, const void *src, int len)
{
    sol_memcpy(dst, src, len);
}
