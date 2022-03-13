// Include the Solana SDK which provides all external functions that this program can call
#include "solana_sdk.h"

// Include all type definitions
#include "types.c"

// Include all pubkey constant definitions
#include "pubkey_constants.c"

// include definition of all errors
#include "error.c"

// These are all instructions that this program can execute
typedef enum Instruction
{
    // Creates a new block, ready to accept entries
    Instruction_CreateBlock              =  1,
    // Adds entries to a newly created block.  Is called multiple times in sequence until the block has had all of its
    // entries added.  When the last entry is added, the block becomes live.
    Instruction_AddEntriesToBlock        =  2,
    // Deletes a block which was created but has not had all of its entries added yet and thus is not live.  This
    // would be for removing "mistake" blocks before they are made live.  Once live, blocks cannot be deleted.
    Instruction_DeleteBlock              =  3,
    // Deletes entries within a block.  Only entries in live blocks may be deleted.  Only entries meeting one of the
    // following conditions may be deleted:
    // - It is an unsold ticket
    // - It is a claimed entry that is owned by Shinobi Systems
    // Thus entries may be deleted only if no one has bought them, or they've been bought and owned by Shinobi
    // Systems.  This gives Shinobi Systems a path to delete entries: buy buying them then destroying them.
    Instruction_DeleteEntries            =  4,
    // Reveals entries that have not been revealed yet.  This can obviously only be done after the block has reached
    // its reveal criteria (either number of tickets sold, or reveal date passed).
    Instruction_RevealEntries            =  5,
    // Buys a ticket
    Instruction_BuyTicket                =  6,
    // Refunds a ticket, which can only be done if the entry is not revealed yet
    Instruction_RefundTicket             =  7,
    // Redeems a ticket, which can only be done after the entry has been revealed
    Instruction_RedeemTicket             =  8,
    // Bids on an unsold entry
    Instruction_Bid                      =  9,
    // Claims a winning or losing bid
    Instruction_Claim                    = 10,
    // Returns a claimed entry in exchange for the stake account.  The returned entry immediately enters a new auction.
    Instruction_Return                   = 11,
    // Harvests Ki
    Instruction_Harvest                  = 12,
    // Levels up an entry.  This requires that the entry has cumulatively earned enough Ki to do so.
    Instruction_LevelUp                  = 13,
    // The owner of an entry can call this to update the id of the metadata program to use to manage metadata to the
    // value in the program config
    Instruction_UpdateMetadataProgramId  = 14,
    // Updates the commission charged per epoch per stake account.  This is in addition to the validator commission.
    // This command fails if the commission has already been updated in the current epoch, and if the old commission
    // is above 5% and the new commission is more than 5% higher than the old commission.
    Instruction_SetBlockCommission       = 15,
    // Takes cumulatively earned commission from a stake account
    Instruction_TakeCommission           = 16,
    // Performs the next step in redelegation for a stake account: if the stake account is delegated but not to
    // Shinobi Systems, a small fee is taken and the stake account is un-delegated.  If the stake account is not
    // delegated, a small fee is taken and the stake account is delegated to Shinobi Systems.
    Instruction_RedelegateTurnCrank      = 17
} Instruction;


// Include helper functions required by all subsequent functions
#include "is_admin_authenticated.c"
#include "create_account_with_seeds.c"

// Split the actual instruction processing up into separate files for ease of use
#include "do_create_block.c"
#include "do_add_entries_to_block.c"
#include "do_delete_block.c"
#include "do_delete_entries.c"
#include "do_reveal_entries.c"
#include "do_buy_ticket.c"
#include "do_refund_ticket.c"
#include "do_redeem_ticket.c"
#include "do_bid.c"
#include "do_claim.c"
#include "do_return.c"
#include "do_harvest.c"
#include "do_level_up.c"
#include "do_update_metadata_program_id.c"
#include "do_set_block_commission.c"
#include "do_take_commission.c"
#include "do_redelegate_turn_crank.c"


// Program entrypoint
uint64_t entrypoint(const uint8_t *input)
{
    SolParameters params;

    // At most 16 accounts are supported for any command
    SolAccountInfo account_info[16];
    params.ka = account_info;
    
    // Deserialize parameters.  Must succeed.
    if (!sol_deserialize(input, &params, 16)) {
        return Error_InvalidData;
    }

    // If there isn't even an instruction index, the instruction is invalid.
    if (params.data_len < 1) {
        return Error_ShortData;
    }

    // Initialize "globals"

    // The instruction index is the first byte of data.  For each instruction code, call the appropriate do_ function
    // to handle that instruction, and return its result.
    switch (params.data[0]) {
    case Instruction_CreateBlock:
        return do_create_block(&params);

    case Instruction_AddEntriesToBlock:
        return do_add_entries_to_block(&params);

    case Instruction_DeleteBlock:
        return do_delete_block(&params);

    case Instruction_DeleteEntries:
        return do_delete_entries(&params);

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

    case Instruction_Harvest:
        return do_harvest(&params);

    case Instruction_LevelUp:
        return do_level_up(&params);

    case Instruction_UpdateMetadataProgramId:
        return do_update_metadata_program_id(&params);

    case Instruction_SetBlockCommission:
        return do_set_block_commission(&params);

    case Instruction_TakeCommission:
        return do_take_commission(&params);

    case Instruction_RedelegateTurnCrank:
        return do_redelegate_turn_crank(&params);

    // If the instruction code is not for a valid instruction, return an error.
    default:
        return Error_UnknownInstruction;
    }
}
