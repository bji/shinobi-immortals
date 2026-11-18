
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
    // commission is more than 1 percentage point higher than the old commission.  No commission can exceed 5%.
    Instruction_SetBlockCommission            =  6,
    // Split stake off of the master stake account.  Will never split off stake that would leave the master account
    // at less than 2 SOL staked.
    Instruction_SplitMasterStake              =  7,
    // Add entries to a block whitelist
    Instruction_AddWhitelistEntries           =  8,
    // Delete a whitelist after it is no longer needed
    Instruction_DeleteWhitelist               =  9,

    // User functions: end users may perform these actions -------------------------------------------------------------
    // Buy, either during mystery period, or after end of auction, or during a "final sale"
    Instruction_Buy                           = 10,
    // Request a refund of an entry that was purchased before reveal, and was not revealed before the reveal grace
    // period completed
    Instruction_Refund                        = 11,
    // Bid on an entry that is in normal auction.  Every bid after the first must exceed the current highest bid by at
    // least MINIMUM_BID_INCREMENT_BPS; a bid placed near the end of the auction extends it by up to
    // AUCTION_EXTENSION_SECONDS.
    Instruction_Bid                           = 12,
    // Claim a losing bid
    Instruction_ClaimLosing                   = 13,
    // Stake the entry, providing a stake account to stake the entry to.  The stake account becomes owned by the
    // program.  The token account holding the entry's token is frozen for as long as the entry is staked, so that
    // the token cannot be transferred or burned while the program holds a stake account for it.
    Instruction_Stake                         = 14,
    // Destake the entry, returning the stake account of the entry, and thawing the token account holding the entry's
    // token.  Ki that the entry has earned is harvested first, unless the instruction data says not to, which makes
    // it possible to recover the stake account even if Ki could not be harvested.  The thaw can be skipped in the same
    // way, which makes it possible to recover the stake account even if the token account could not be thawed; the
    // token account is then left frozen until Thaw thaws it.
    Instruction_Destake                       = 15,
    // Thaw the token account holding the entry's token, if it was left frozen by a Destake that skipped the thaw.
    // Only the token's owner may do this, and only while the entry is not staked.
    Instruction_Thaw                          = 16,
    // Harvest Ki
    Instruction_Harvest                       = 17,
    // Level up an entry.  This requires as input an amount of Ki, which is burned.  If the entry is staked, the Ki
    // that it has earned is harvested first, so that it is paid at the ki_factor of the level at which it was earned.
    Instruction_LevelUp                       = 18,

    // Anyone functions: anyone may perform these actions --------------------------------------------------------------
    // Settle the winning bid of an auction that has ended: the entry's token goes to the winning bidder's associated
    // token account, and the winning bid goes to the admin.  The winning bidder may do this at any time; anyone else
    // may do it once 30 days have passed since the end of the auction, so that an auction which was won can always
    // be completed, even if the bidder never returns.
    Instruction_SettleWinningBid              = 19,
    // If the entry is staked and the stake account is delegated, this pays any commission owed into the master stake
    // account.  If the entry is staked and the stake account is not delegated, this delegates the stake account to
    // Shinobi Systems so that it can start earning Ki.
    Instruction_TakeCommissionOrDelegate      = 20,

    // Special functions -----------------------------------------------------------------------------------------------
    // This is a special function that can only be called on an entry that is in the Owned or OwnedAndStaked state.
    // The transaction must be signed by both the admin and the user who owns the entry.  This instruction will reset
    // the metadata update authority to a new authority, and if the entry is staked, will also return the stake
    // account to the user who owns the entry (without harvesting Ki or charging commission), leaving the entry
    // not staked.  The goal of this is to allow a safety valve in case of bugs or problems with this program; or a
    // need to upgrade the program to handle new conditions.  The program
    // can't be upgraded but a new program can be made and then given authority over all user owned entries via this
    // instruction (but only if both the user and admin agree to do so).  The instruction data includes a flag
    // that says whether or not to reauthorize the metadata; if it is false, the Metaplex Metadata program is not
    // invoked, so that a problem with that program cannot prevent a stake account from being returned.
    Instruction_ReAuthorize                   = 21,

    // Instructions added after ReAuthorize ----------------------------------------------------------------------------
    // Anyone function: delegates the master stake account to Shinobi Systems if it is not actively delegated.  The
    // only way that can come about is that the stake program's DeactivateDelinquent instruction was used to
    // deactivate it, which anyone may do after Shinobi Systems has not voted for five epochs.
    Instruction_DelegateMasterStake           = 22,
    // Admin function: withdraws to the admin the excess lamports of an account that the program controls: the
    // master stake account, the program state account, the authority account, or a block, entry or whitelist
    // account.  The excess is whatever the account holds beyond what it is required to hold; for the master stake
    // account that is its lamports that are not staked, less its minimum of 2 SOL, and for the authority account it
    // excludes the purchase prices of mysteries that it holds in escrow.  This recovers lamports that someone
    // transferred to such an account, which no other instruction can take out again.
    Instruction_WithdrawExcess                = 23

} Instruction;


// The actual instruction processing functions have been split up into separate files for ease of use
#include "super/super_initialize.c"
#include "super/super_set_admin.c"

#include "admin/admin_create_block.c"
#include "admin/admin_add_entries_to_block.c"
#include "admin/admin_set_metadata_bytes.c"
#include "admin/admin_reveal_entries.c"
#include "admin/admin_set_block_commission.c"
#include "admin/admin_split_master_stake.c"
#include "admin/admin_add_whitelist_entries.c"
#include "admin/admin_delete_whitelist.c"
#include "admin/admin_withdraw_excess.c"

#include "user/user_buy.c"
#include "user/user_refund.c"
#include "user/user_bid.c"
#include "user/user_claim_losing.c"
#include "user/user_stake.c"
#include "user/user_destake.c"
#include "user/user_thaw.c"
#include "user/user_harvest.c"
#include "user/user_level_up.c"

#include "anyone/anyone_settle_winning_bid.c"
#include "anyone/anyone_take_commission_or_delegate.c"
#include "anyone/anyone_delegate_master_stake.c"

#include "special/special_reauthorize.c"


// Program entrypoint
uint64_t entrypoint(const uint8_t *input)
{
    SolParameters params;

    // At most 21 accounts are supported for any command.  This is enough for 3 entries in the "add entries to block"
    // instruction.
    SolAccountInfo account_info[21];
    params.ka = account_info;

    // Deserialize parameters.  Must succeed.
    if (!sol_deserialize(input, &params, ARRAY_LEN(account_info))) {
        return Error_InvalidData;
    }

    // If there isn't even an instruction index, the instruction is invalid.
    if (params.data_len < 1) {
        return Error_InvalidDataSize;
    }

    // The instruction index is the first byte of data.  For each instruction code, call the appropriate function to
    // handle that instruction, and return its result.
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

    case Instruction_SetBlockCommission:
        return admin_set_block_commission(&params);

    case Instruction_SplitMasterStake:
        return admin_split_master_stake(&params);

    case Instruction_AddWhitelistEntries:
        return admin_add_whitelist_entries(&params);

    case Instruction_DeleteWhitelist:
        return admin_delete_whitelist(&params);

    case Instruction_Buy:
        return user_buy(&params);

    case Instruction_Refund:
        return user_refund(&params);

    case Instruction_Bid:
        return user_bid(&params);

    case Instruction_ClaimLosing:
        return user_claim_losing(&params);

    case Instruction_Stake:
        return user_stake(&params);

    case Instruction_Destake:
        return user_destake(&params);

    case Instruction_Thaw:
        return user_thaw(&params);

    case Instruction_Harvest:
        return user_harvest(&params);

    case Instruction_LevelUp:
        return user_level_up(&params);

    case Instruction_SettleWinningBid:
        return anyone_settle_winning_bid(&params);

    case Instruction_TakeCommissionOrDelegate:
        return anyone_take_commission_or_delegate(&params);

    case Instruction_ReAuthorize:
        return special_reauthorize(&params);

    case Instruction_DelegateMasterStake:
        return anyone_delegate_master_stake(&params);

    case Instruction_WithdrawExcess:
        return admin_withdraw_excess(&params);

    default:
        return Error_UnknownInstruction;
    }
}


// Provide a memcpy implementation; this allows structure assignments that the compiler will turn into memcpy which is
// safer than calling memcpy directly
void *memcpy(void *dst, const void *src, int len)
{
    (void) sol_memcpy(dst, src, len);
    return dst;
}
