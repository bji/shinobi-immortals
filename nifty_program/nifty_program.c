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
    // Creates a new block
    Instruction_CreateBlock              =  0,
    // Adds entries to a newly created block.  Is called multiple times in sequence until the block has had all of its
    // entries added.  When the last entry is added, the block becomes live.
    Instruction_AddEntriesToBlock        =  1,
    // Deletes a block which was created but has not had all of its entries added yet and thus is not live.  This
    // would be for removing "mistake" blocks before they are made live.  Once live, blocks cannot be deleted.
    Instruction_DeleteBlock              =  2,
    // Reveals entries that have not been revealed yet.  This can obviously only be done after the block has reached
    // its reveal criteria (either number of tickets sold, or reveal date passed).
    Instruction_RevealEntries            =  3,
    // Buys a ticket
    Instruction_BuyTicket                =  4,
    // Refunds a ticket, which can only be done if the entry is not revealed yet
    Instruction_RefundTicket             =  5,
    // Redeems a ticket, which can only be done after the entry has been revealed
    Instruction_RedeemTicket             =  6,
    // Bids on an unsold entry
    Instruction_Bid                      =  7,
    // Claims a winning or losing bid
    Instruction_Claim                    =  8,
    // Returns a claimed entry in exchange for the stake account.  The returned entry immediately enters a new auction.
    Instruction_Return                   =  9,
    // Merge stake into the stake account backing an entry.  This allows users to put more stake behind an owned entry
    // whenever they want to (presumably to earn Ki faster and level up faster)
    Instruction_MergeStake               = 10,
    // Split stake from the stake account backing an entry.  This allows users to extract stake rewards earned by the
    // entry.  Can only split off earned rewards, not principal.
    Instruction_SplitStake               = 11,
    // Harvests Ki
    Instruction_Harvest                  = 12,
    // Levels up an entry.  This requires that the entry has cumulatively earned enough Ki to do so.
    Instruction_LevelUp                  = 13,
    // Permanently buys an owned entry, by paying the permabuy fee.  The owner of the entry gets their stake
    // account back (minus fee), and retains the NFT itself.  Once permabought, an entry can never be returned,
    // and thus can never be levelled up again.
    Instruction_Permanently_Buy          = 14,
    // The owner of an entry can call this to update the id of the metadata program to use to manage metadata to the
    // value in the program config
    Instruction_UpdateMetadataProgramId  = 15,
    // Updates the commission charged per epoch per stake account.  This is in addition to the validator commission.
    // This command fails if the commission has already been updated in the current epoch, and if the old commission
    // is above 5% and the new commission is more than 5% higher than the old commission.
    Instruction_SetBlockCommission       = 16,
    // Takes cumulatively earned commission from a stake account
    Instruction_TakeCommission           = 17,
    // Performs the next step in redelegation for a stake account: if the stake account is delegated but not to
    // Shinobi Systems, a small fee is taken and the stake account is un-delegated.  If the stake account is not
    // delegated, a small fee is taken and the stake account is delegated to Shinobi Systems.
    Instruction_RedelegateTurnCrank      = 18
} Instruction;


// Include helper functions required by all subsequent functions
#include "is_admin_authenticated.c"
#include "create_pda.c"
#include "find_block_address.c"
#include "compute_block_size.c"

// Split the actual instruction processing up into separate files for ease of use
#include "do_create_block.c"
#include "do_add_entries_to_block.c"
#include "do_delete_block.c"
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

    // The instruction index is the first byte of data.  For each instruction code, call the appropriate do_ function
    // to handle that instruction, and return its result.
    switch (params.data[0]) {
    case Instruction_CreateBlock:
        return do_create_block(&params);

    case Instruction_AddEntriesToBlock:
        return do_add_entries_to_block(&params);

    case Instruction_DeleteBlock:
        return do_delete_block(&params);

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
