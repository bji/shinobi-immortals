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
    // Add metadata program id
    Instruction_AddMetadataProgramId          =  2,
    // Create a new block
    Instruction_CreateBlock                   =  3,
    // Add entries to a newly created block.  Is called multiple times in sequence until the block has had all of its
    // entries added.  When the last entry is added, the block becomes live
    Instruction_AddEntriesToBlock             =  4,
    // Set bytes of metadata in an entry.  This can only be done after the entry's block has reached its reveal
    // criteria.
    Instruction_SetMetadataBytes              =  5,
    // Reveal entries that have not been revealed yet.  This can only be done after the block has reached its reveal
    // criteria, and the entries have had their metadata set.
    Instruction_RevealEntries                 =  6,
    // Update the commission charged per epoch per stake account.  This is in addition to the validator commission.
    // This command fails if the commission has already been updated in the current epoch, and if the old commission
    // is above 5% and the new commission is more than 2% higher than the old commission.
    Instruction_SetBlockCommission            =  7,
    // Take cumulatively earned commission from a stake account
    Instruction_TakeCommission                =  8,

    // User functions: end users may perform these actions -------------------------------------------------------------
    // Buy a ticket
    Instruction_BuyTicket                     =  9,
    // Redeem a ticket, which if done after reveal, redeems for the entry NFT; if done before reveal and after
    // the reveal grace period (i.e. reveal didn't happen on time), returns the stake account.  If done before
    // reveal and before reveal grace, error.
    Instruction_RedeemTicket                  = 10,
    // Bid on an entry that is in auction
    Instruction_Bid                           = 11,
    // Claim a winning or losing bid
    Instruction_Claim                         = 12,
    // Return am owned entry in exchange for the stake account.  The returned entry immediately enters
    // a new auction.
    Instruction_Return                        = 13,
    // Merge stake into the stake account backing an entry.  This allows users to put more stake behind
    // an owned entry whenever they want to (presumably to earn Ki faster and level up faster)
    Instruction_MergeStake                    = 14,
    // Split stake from the stake account backing an entry.  This allows users to extract stake rewards
    // earned by the entry.  It is free to split stake earnings off.  It costs commission to split principal off.
    Instruction_SplitStake                    = 15,
    // Harvest Ki
    Instruction_Harvest                       = 16,
    // Level up an entry.  This requires that the entry has cumulatively earned enough Ki to do so.
    Instruction_LevelUp                       = 17,
    // Update the metadata program id of an entry.  This will only update to the next metadata entry id from the
    // program config after the current metadata program id (or the first one if the current one is empty).  It will
    // also call that metdata program to do its initial update of the data, and if that succeeds, will set the
    // metaplex metadata update authority to that program.  All future metadata updates will be through that program
    Instruction_UpdateMetadataProgramId       = 18,

    // Anyone functions: anyone may perform these actions --------------------------------------------------------------
    // Perform the next step in redelegation for a stake account: if the stake account is delegated but not to Shinobi
    // Systems, a small fee is taken and the stake account is un-delegated.  If the stake account is not delegated, a
    // small fee is taken and the stake account is delegated to Shinobi Systems.
    Instruction_RedelegateTurnCrank           = 19
} Instruction;


// The actual instruction processing functions have been split up into separate files for ease of use
#include "admin/admin_add_entries_to_block.c"
#include "admin/admin_add_metadata_program_id.c"
#include "admin/admin_create_block.c"
//#include "admin/admin_reveal_entries.c"
//#include "admin/admin_set_block_commission.c"
//#include "admin/admin_set_metadata_bytes.c"
//#include "admin/admin_take_commission.c"

//#include "user/user_bid.c"
//#include "user/user_buy_ticket.c"
//#include "user/user_claim.c"
//#include "user/user_harvest.c"
//#include "user/user_level_up.c"
//#include "user/user_merge_stake.c"
//#include "user/user_redeem_ticket.c"
//#include "user/user_return.c"
//#include "user/user_split_stake.c"
//#include "user/user_update_metadata_program_id.c"

#include "super/super_initialize.c"
#include "super/super_set_admin.c"


// Program entrypoint
uint64_t entrypoint(const uint8_t *input)
{
    // Initialize Constants
//    const Constants constants =
//        {
//            // superuser_pubkey
//            // DEVNET test value: dspaQ8kM3myRapUFqw8zRgwHJMUB5YcKKh7CxA1MWF1
//            { 0x09, 0x72, 0x5f, 0x2d, 0xcf, 0x16, 0x13, 0x1c, 0x3d, 0x71, 0x3d, 0xf1, 0xf3, 0x66, 0x09, 0xc4,
//              0xa1, 0xc7, 0x39, 0xff, 0xfe, 0xeb, 0x8e, 0x04, 0x67, 0x5f, 0x44, 0x59, 0x37, 0xb7, 0x37, 0x40 },
//
//            // nifty_config_account
//            // DEVNET test value: CqvEvXCZnJcsqsfBLYY5SuEyzZZPGEYNo1i2eoo3oiUE
//            { 0xaf, 0xf8, 0xa2, 0x3c, 0xbe, 0xd5, 0x5d, 0xd0, 0x40, 0x10, 0x24, 0x04, 0x51, 0x59, 0xf8, 0xb3,
//              0x3f, 0x64, 0x35, 0x23, 0x69, 0x0c, 0xa4, 0xdb, 0x6a, 0x22, 0xaa, 0x76, 0x30, 0xfe, 0x5f, 0xff },
//
//            // nifty_config_seed_bytes
//            { PDA_Account_Seed_Prefix_Config, 252 },
//
//            // nifty_authority_account
//            // DEVNET test value: 4T6BDsr5AZDmf351qyQ29Ww2J64oJmxEsWtH1S58JWZE
//            { 0x33, 0x42, 0x03, 0xbd, 0x7b, 0x62, 0x4b, 0x99, 0x57, 0xc7, 0xaf, 0x26, 0x3c, 0x48, 0x1e, 0xfa,
//              0xf6, 0x40, 0xd3, 0xf1, 0x29, 0xe4, 0x6d, 0x8e, 0xb0, 0x01, 0x3f, 0x57, 0x6e, 0x6b, 0xe1, 0x39 },
//
//            // nifty_authority_seed_bytes
//            { PDA_Account_Seed_Prefix_Authority, 255 },
//
//            // nifty_program_id
//            // DEVNET test value: dnpHN9oTti7DAoZMw7WL8PvXWuL5Q4BZeVtcEevzaGa
//            { 0x09, 0x6c, 0xb6, 0x69, 0xa0, 0x04, 0xa6, 0x98, 0x0e, 0x80, 0x7d, 0x43, 0x4e, 0xb0, 0xa6, 0x04,
//              0x00, 0x63, 0xfa, 0xd5, 0x9b, 0x64, 0x37, 0xcc, 0xe5, 0xa6, 0x8f, 0x93, 0x23, 0x04, 0x9c, 0x43 },
//
//            // system_program_id
//            // 11111111111111111111111111111111
//            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
//              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
//
//            // rent_program_id
//            // SysvarRent111111111111111111111111111111111
//            { 0x06, 0xa7, 0xd5, 0x17, 0x19, 0x2c, 0x5c, 0x51, 0x21, 0x8c, 0xc9, 0x4c, 0x3d, 0x4a, 0xf1, 0x7f,
//              0x58, 0xda, 0xee, 0x08, 0x9b, 0xa1, 0xfd, 0x44, 0xe3, 0xdb, 0xd9, 0x8a, 0x00, 0x00, 0x00, 0x00 },
//
//            // metaplex_program_id
//            // metaqbxxUerdq28cj1RbAWkYQm3ybzjb6a8bt518x1s
//            { 0x0b, 0x70, 0x65, 0xb1, 0xe3, 0xd1, 0x7c, 0x45, 0x38, 0x9d, 0x52, 0x7f, 0x6b, 0x04, 0xc3, 0xcd,
//              0x58, 0xb8, 0x6c, 0x73, 0x1a, 0xa0, 0xfd, 0xb5, 0x49, 0xb6, 0xd1, 0xbc, 0x03, 0xf8, 0x29, 0x46 },
//
//            // spl_token_program_id
//            // TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA
//            { 0x06, 0xdd, 0xf6, 0xe1, 0xd7, 0x65, 0xa1, 0x93, 0xd9, 0xcb, 0xe1, 0x46, 0xce, 0xeb, 0x79, 0xac,
//              0x1c, 0xb4, 0x85, 0xed, 0x5f, 0x5b, 0x37, 0x91, 0x3a, 0x8c, 0xf5, 0x85, 0x7e, 0xff, 0x00, 0xa9 }
//        };
    
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
        
    case Instruction_AddMetadataProgramId:
        return admin_add_metadata_program_id(&params);
        
    case Instruction_CreateBlock:
        return admin_create_block(&params);

    case Instruction_AddEntriesToBlock:
        return admin_add_entries_to_block(&params);

//    case Instruction_SetMetadataBytes:
//        return admin_set_metadata_bytes(&params);
//        
//    case Instruction_RevealEntries:
//        return admin_reveal_entries(&params);
//        
//    case Instruction_SetBlockCommission:
//        return admin_set_block_commission(&params);
//        
//    case Instruction_TakeCommission:
//        return admin_take_commission(&params);
//        
//    case Instruction_BuyTicket:
//        return user_buy_ticket(&params);
//        
//    case Instruction_RedeemTicket:
//        return user_redeem_ticket(&params);
//        
//    case Instruction_Bid:
//        return user_bid(&params);
//        
//    case Instruction_Claim:
//        return user_claim(&params);
//        
//    case Instruction_Return:
//        return user_return(&params);
//        
//    case Instruction_MergeStake:
//        return user_merge_stake(&params);
//        
//    case Instruction_SplitStake:
//        return user_split_stake(&params);
//        
//    case Instruction_Harvest:
//        return user_harvest_ki(&params);
//        
//    case Instruction_LevelUp:
//        return user_level_up(&params);
//        
//    case Instruction_UpdateEntryMetadataProgramId:
//        return user_update_metadata_program_id(&params);
//        
//    case Instruction_RedelegateTurnCrank:
//        return anyone_redelegate_turn_crank(&params);
        
    default:
        return Error_UnknownInstruction;
    }
}
