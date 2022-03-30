// Include the Solana SDK which provides all external functions that this program can call
#include "solana_sdk.h"

// Functions missing from solana_sdk.h
extern uint64_t sol_get_clock_sysvar(void *ret);

// Include all type definitions
#include "types.c"

// Include all pubkey constant definitions
#include "pubkey_constants.c"

// include definition of all errors
#include "error.c"

// These are all instructions that this program can execute
typedef enum Instruction
{
    // Super user functions: only the super user may perform these actions ---------------------------------------------
    // Create admin account
    Instruction_CreateAdminAccount            =  0,
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
    // Delete a block which was created but has not had all of its entries added yet and thus is not live.  This would
    // be for removing "mistake" blocks before they are made live.  Once live, blocks cannot be deleted.
    Instruction_DeleteBlock                   =  5,
    // Set bytes of metadata in an entry.  This can only be done after the entry's block has reached its reveal
    // criteria.
    Instruction_SetMetadataBytes              =  6,
    // Reveal entries that have not been revealed yet.  This can only be done after the block has reached its reveal
    // criteria.
    Instruction_RevealEntries                 =  7,
    // Update the commission charged per epoch per stake account.  This is in addition to the validator commission.
    // This command fails if the commission has already been updated in the current epoch, and if the old commission
    // is above 5% and the new commission is more than 2% higher than the old commission.
    Instruction_SetBlockCommission            =  8,
    // Take cumulatively earned commission from a stake account
    Instruction_TakeCommission                =  9,

    // User functions: end users may perform these actions -------------------------------------------------------------
    // Buy a ticket
    Instruction_BuyTicket                     = 10,
    // Refund a ticket, which can only be done if the entry is not revealed yet but the reveal grace period has
    // elapsed
    Instruction_RefundTicket                  = 11,
    // Redeem a ticket, which can only be done after the entry has been revealed
    Instruction_RedeemTicket                  = 12,
    // Bid on an entry that is in auction
    Instruction_Bid                           = 13,
    // Claim a winning or losing bid
    Instruction_Claim                         = 14,
    // Return am owned entry in exchange for the stake account.  The returned entry immediately enters
    // a new auction.
    Instruction_Return                        = 15,
    // Merge stake into the stake account backing an entry.  This allows users to put more stake behind
    // an owned entry whenever they want to (presumably to earn Ki faster and level up faster)
    Instruction_MergeStake                    = 16,
    // Split stake from the stake account backing an entry.  This allows users to extract stake rewards
    // earned by the entry.  It is free to split stake earnings off.  It costs commission to split principal off.
    Instruction_SplitStake                    = 17,
    // Harvest Ki
    Instruction_Harvest                       = 18,
    // Level up an entry.  This requires that the entry has cumulatively earned enough Ki to do so.
    Instruction_LevelUp                       = 19,
    // Update the metadata program id of an entry.  This will only update to the next metadata entry id from the
    // program config after the current metadata program id (or the first one if the current one is empty).  It will
    // also call that metdata program to do its initial update of the data, and if that succeeds, will set the
    // metaplex metadata update authority to that program.  All future metadata updates will be through that program
    Instruction_UpdateEntryMetadataProgramId  = 20,

    // Anyone functions: anyone may perform these actions --------------------------------------------------------------
    // Perform the next step in redelegation for a stake account: if the stake account is delegated but not to Shinobi
    // Systems, a small fee is taken and the stake account is un-delegated.  If the stake account is not delegated, a
    // small fee is taken and the stake account is delegated to Shinobi Systems.
    Instruction_RedelegateTurnCrank           = 21
} Instruction;


// Included from common implementation
#include "program_config.c"

// Include helper functions required by all subsequent functions
#include "is_admin_authenticated.c"
#include "is_block_complete.c"
#include "is_block_reveal_criteria_met.c"
#include "is_empty_pubkey.c"
#include "create_pda.c"
#include "find_block_address.c"
#include "get_metaplex_metadata_account.c"
#include "reveal_single_entry.c"

// Split the actual instruction processing up into separate files for ease of use
#include "do_create_block.c"
#include "do_add_entries_to_block.c"
#include "do_delete_block.c"
#include "do_set_metadata_bytes.c"
#include "do_reveal_entries.c"
#include "do_buy_ticket.c"
#include "do_refund_ticket.c"
#include "do_redeem_ticket.c"
#include "do_bid.c"
#include "do_claim.c"
#include "do_return.c"
#include "do_merge_stake.c"
#include "do_split_stake.c"
#include "do_harvest.c"
#include "do_level_up.c"
#include "do_set_block_commission.c"
#include "do_take_commission.c"
#include "do_redelegate_turn_crank.c"


// Program entrypoint
uint64_t entrypoint(const uint8_t *input)
{
    SolParameters params;

    // At most 23 accounts are supported for any command
    SolAccountInfo account_info[23];
    params.ka = account_info;
    
    // Deserialize parameters.  Must succeed.
    if (!sol_deserialize(input, &params, 23)) {
        return Error_InvalidData;
    }

    // If there isn't even an instruction index, the instruction is invalid.
    if (params.data_len < 1) {
        return Error_InvalidDataSize;
    }

    // The instruction index is the first byte of data.  For each instruction code, call the appropriate do_ function
    // to handle that instruction, and return its result.
    switch (params.data[0]) {
    case Instruction_CreateBlock:
        return do_create_block(&params);

    case Instruction_AddEntriesToBlock:
        return do_add_entries_to_block(&params);

    case Instruction_DeleteBlock:
        return do_delete_block(&params);

    case Instruction_SetMetadataBytes:
        return do_set_metadata_bytes(&params);

    case Instruction_RevealEntries:
        return do_reveal_entries(&params);

    case Instruction_BuyTicket:
        return do_buy_ticket(&params);

    case Instruction_RefundTicket:
        return do_refund_ticket(&params);

    case Instruction_RedeemTicket:
        return do_redeem_ticket(&params);

    case Instruction_Bid:
        return do_bid(&params);

    case Instruction_Claim:
        return do_claim(&params);

    case Instruction_Return:
        return do_return(&params);

    case Instruction_MergeStake:
        return do_merge_stake(&params);

    case Instruction_SplitStake:
        return do_split_stake(&params);
        
    case Instruction_Harvest:
        return do_harvest(&params);

    case Instruction_LevelUp:
        return do_level_up(&params);

    case Instruction_SetBlockCommission:
        return do_set_block_commission(&params);

    case Instruction_TakeCommission:
        return do_take_commission(&params);

    case Instruction_RedelegateTurnCrank:
        return do_redelegate_turn_crank(&params);

    default:
        return Error_UnknownInstruction;
    }
}
