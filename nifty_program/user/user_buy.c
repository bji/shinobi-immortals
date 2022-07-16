#pragma once

#include "inc/types.h"
#include "util/util_token.c"
#include "util/util_transfer_lamports.c"


// If start_price > 100,000 SOL, rounding errors could be significant.
static uint64_t compute_price(uint64_t total_seconds, uint64_t start_price, uint64_t end_price,
                              uint64_t seconds_elapsed);


static uint64_t user_buy(SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,   funding_account,                  ReadWrite,  Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(1,   config_account,                   ReadOnly,   NotSigner,  KnownAccount_ProgramConfig);
        DECLARE_ACCOUNT(2,   admin_account,                    ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(3,   authority_account,                ReadOnly,   NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(4,   block_account,                    ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(5,   entry_account,                    ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(6,   entry_token_account,              ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(7,   entry_mint_account,               ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(8,   token_destination_account,        ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(9,   token_destination_owner_account,  ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(10,  entry_metadata_account,           ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(11,  nifty_program_account,            ReadOnly,   NotSigner,  KnownAccount_NiftyProgram);
        DECLARE_ACCOUNT(12,  spl_token_program_account,        ReadOnly,   NotSigner,  KnownAccount_SPLTokenProgram);
        DECLARE_ACCOUNT(13,  spl_ata_program_account,          ReadOnly,   NotSigner,  KnownAccount_SPLATAProgram);
        DECLARE_ACCOUNT(14,  metaplexprogram_account,          ReadOnly,   NotSigner,  KnownAccount_MetaplexProgram);
        DECLARE_ACCOUNT(15,  system_program_account,           ReadOnly,   NotSigner,  KnownAccount_SystemProgram);
    }
    
    DECLARE_ACCOUNTS_NUMBER(16);
    
    // This is the block data
    Block *block = get_validated_block(block_account);
    if (!block) {
        return Error_InvalidAccount_First + 4;
    }

    // Ensure that the block is complete; cannot buy anything from a block that is not complete yet
    if (!is_block_complete(block)) {
        return Error_BlockNotComplete;
    }

    // This is the entry data
    Entry *entry = get_validated_entry_of_block(entry_account, block_account->key);
    if (!entry) {
        return Error_InvalidAccount_First + 5;
    }

    // Check that the correct token account address is supplied
    if (!SolPubkey_same(entry_token_account->key, &(entry->token_pubkey))) {
        return Error_InvalidAccount_First + 6;
    }

    // Check that the correct mint account address is supplied
    if (!SolPubkey_same(entry_mint_account->key, &(entry->mint_pubkey))) {
        return Error_InvalidAccount_First + 7;
    }
    
    // Check that the correct metaplex metadata account is supplied
    if (!SolPubkey_same(entry_metadata_account->key, &(entry->metaplex_metadata_pubkey))) {
        return Error_InvalidAccount_First + 10;
    }

    // Get the clock sysvar, needed below
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }

    // No need to check that the token is owned by the nifty stakes program; if it is not, then the SPL token program
    // invoke of the transfer of the token from the token account to the destination account will simply fail and the
    // transaction will then fail

    SolAccountInfo *funds_destination_account;
    uint64_t purchase_price_lamports;

    switch (get_entry_state(block, entry, &clock)) {
    case EntryState_PreRevealOwned:
    case EntryState_WaitingForRevealOwned:
    case EntryState_Owned:
    case EntryState_OwnedAndStaked:
        // Already owned, can't be purchased again
        return Error_AlreadyOwned;

    case EntryState_WaitingForRevealUnowned:
        // In reveal grace period waiting for reveal, can't be purchased
        return Error_EntryWaitingForReveal;

    case EntryState_InAuction:
        // In auction, can't be purchased, can only be bid on
        return Error_EntryInAuction;

    case EntryState_WaitingToBeClaimed:
        // Has a winning auction bid, can't be purchased
        return Error_EntryWaitingToBeClaimed;
        
    case EntryState_PreRevealUnowned:
        // Pre-reveal but not owned yet.  Can be purchased as a mystery.

        // The authority account must be writable so that the purchase price lamports can be added to it
        if (!authority_account->is_writable) {
            return Error_InvalidAccountPermissions_First + 3;
        }

        // The block account must be writable so that the mysteries sold counter can be incremented
        if (!block_account->is_writable) {
            return Error_InvalidAccountPermissions_First + 4;
        }

        // The destination of funds is the authority account, which is where the purchase price is stored until
        // the entry is revealed (and if the entry is never revealed and the user requests a refund, then the
        // funds are removed from that account and returned back to the user)
        funds_destination_account = authority_account;
        
        // Compute price of mystery
        purchase_price_lamports = compute_price(block->config.mystery_phase_duration,
                                                block->config.mystery_start_price_lamports,
                                                block->config.minimum_price_lamports,
                                                clock.unix_timestamp - block->block_start_timestamp);

        // Update the entry's block to indicate that one more mystery was purchased.  If this is the last
        // mystery to purchase before the block becomes revealable, then the block reveal period begins.
        block->mysteries_sold_count += 1;
        if (block->mysteries_sold_count == block->config.total_mystery_count) {
            block->mystery_phase_end_timestamp = clock.unix_timestamp;
        }

        break;

    case EntryState_Unowned:
        // Unowned, revealed, and not in an auction.  Can be purchased.

        // The write state of the nifty authority and block accounts indicates whether the user intends to buy a
        // mystery or a non-mystery.  Fail the buy if their desired mystery state doesn't match the state of the
        // entry, to prevent a user with stale information from buying what they think is a mystery when it no longer
        // is.
        if (authority_account->is_writable) {
            return Error_InvalidAccountPermissions_First + 3;
        }
        if (block_account->is_writable) {
            return Error_InvalidAccountPermissions_First + 4;
        }
        
        // The destination of funds is the admin account
        funds_destination_account = admin_account;

        // Compute purchase price.  If the entry had an auction, then being unowned means that it was never bid on,
        // so its price post-auction is the minimum price
        if (entry->has_auction) {
            purchase_price_lamports = block->config.minimum_price_lamports;
        }
        // Else, use compute_price to compute the price of a non-auction entry
        else {
            purchase_price_lamports = compute_price(entry->duration, entry->non_auction_start_price_lamports,
                                                    entry->minimum_price_lamports,
                                                    clock.unix_timestamp - entry->reveal_timestamp);
        }

        break;

    case EntryState_PreReveal:
        // This case isn't possible because a block was provided to get_entry_state
        return Error_InternalProgrammingError;
    }

    // Check to ensure that the funds source has at least the purchase price lamports
    if (*(funding_account->lamports) < purchase_price_lamports) {
        return Error_InsufficientFunds;
    }

    // Transfer the purchase price from the funds source to the funds destination account
    uint64_t ret = util_transfer_lamports(funding_account->key, funds_destination_account->key, purchase_price_lamports,
                                          params->ka, params->ka_num);
    if (ret) {
        return ret;
    }

    // Ensure that the token destination account exists
    ret = create_associated_token_account_idempotent(token_destination_account, token_destination_owner_account->key,
                                                     &(entry->mint_pubkey), funding_account->key, params->ka,
                                                     params->ka_num);
    if (ret) {
        return ret;
    }
                                          
    // Transfer the token to the token destination account
    ret = transfer_entry_token(entry, token_destination_account, params->ka, params->ka_num);
    if (ret) {
        return ret;
    }

    // Set the purchase price in the Entry now that it's been purchased
    entry->purchase_price_lamports = purchase_price_lamports;

    // And set the primary_sale_happened flag on the metaplex metadata, which isn't strictly necessary but just
    // in case there are UI presentations that care
    ret = set_metaplex_metadata_primary_sale_happened(entry, params->ka, params->ka_num);
    if (ret) {
        return ret;
    }
    
    // Finally, close the entry's token account since it will never be used again.  The lamports go to the admin
    // account.
    return close_entry_token(entry, admin_account->key, params->ka, params->ka_num);
}


static uint64_t compute_price(uint64_t total_seconds, uint64_t start_price, uint64_t end_price,
                              uint64_t seconds_elapsed)
{
    // Once the elapsed seconds hits the total seconds, the result is always end_price 
    if (seconds_elapsed >= total_seconds) {
        return end_price;
    }

    uint64_t delta = start_price - end_price;

    // This is a curve based on the formula: y = (1 / (100x + 1)) - (1 / 101)
    // To avoid rounding errors in math, work with lamports / 1000
    delta /= 1000ull;
    end_price /= 1000ull;

    uint64_t ac = delta * 101ull;

    uint64_t ab = ((100ull * delta * seconds_elapsed) / total_seconds) + delta;

    uint64_t bc = ((100ull * 101ull * seconds_elapsed) / total_seconds) + 101ull;

    return (end_price + ((ac - ab) / bc)) * 1000ull;
}
