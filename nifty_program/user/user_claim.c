#pragma once

// Account references:
// 0. `[WRITE if claiming losing bid, SIGNER]` -- The account which originally created the bid
// 1. `[]` -- The block account
// 2. `[WRITE if claiming winning bid]` -- The entry account
// 3. `[WRITE]` -- The bid PDA account
//
// If this is a claim of a winning bid:
// 
// 4. `[]` -- The config account
// 5. `[WRITE]` -- The admin account
// 6. `[WRITE]` -- Entry token account
// 7. `[]` -- The nifty authority account
// 8. `[WRITE]` -- The account to give ownership of the entry (token) to.  It must already exist and be a
//     valid token account for the entry mint.
// 9. `[]` -- The SPL-Token program id (for cross-program invoke)

typedef struct
{
    // This is the instruction code for Buy
    uint8_t instruction_code;

    // This is the entry_index of the entry within its block
    uint16_t entry_index;

    // This is the bump seed of the bid PDA account
    uint8_t bid_account_bump_seed;
    
} ClaimData;


static uint64_t user_claim(SolParameters *params)
{
    // Sanitize the accounts.  There must be at least 4.
    if (params->ka_num < 4) {
        return Error_IncorrectNumberOfAccounts;
    }
    
    SolAccountInfo *bidding_account = &(params->ka[0]);
    SolAccountInfo *block_account = &(params->ka[1]);
    SolAccountInfo *entry_account = &(params->ka[2]);
    SolAccountInfo *bid_account = &(params->ka[3]);

    // Make sure that the input data is the correct size
    if (params->data_len != sizeof(ClaimData)) {
        sol_log_64(sizeof(ClaimData), params->data_len, 0, 0, 0);
        return Error_InvalidDataSize;
    }

    // Cast to instruction data
    ClaimData *data = (ClaimData *) params->data;

    // Check permissions
    if (!bidding_account->is_signer) {
        return Error_InvalidAccountPermissions_First;
    }
    if (!bid_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 3;
    }

    // This is the block data
    Block *block = get_validated_block(block_account);
    if (!block) {
        return Error_InvalidAccount_First + 1;
    }

    // Ensure that the block is complete; cannot claim anything from a block that is not complete yet
    if (!is_block_complete(block)) {
        return Error_BlockNotComplete;
    }

    // This is the entry data
    Entry *entry = get_validated_entry(block, data->entry_index, entry_account);
    if (!entry) {
        return Error_InvalidAccount_First + 2;
    }

    // Compute the bid account and make sure that the provided bid account was it; and also that the bid account
    // exists (has a nonzero lamports balance)
    SolPubkey computed_bid_address;
    if (!compute_bid_address(bidding_account->key, block->config.group_number, block->config.block_number,
                             entry->entry_index, data->bid_account_bump_seed, &computed_bid_address) ||
        !SolPubkey_same(bid_account->key, &computed_bid_address) ||
        (*(bid_account->lamports) == 0)) {
        return Error_InvalidAccount_First + 3;
    }

    // Get the clock sysvar, needed below
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }

    // If this is the winning bid pubkey, then transfer the token to the winner and transfer the bid lamports to
    // the admin account
    if (SolPubkey_same(bid_account->key, &(entry->auction.winning_bid_pubkey))) {
        // The additional accounts must be present
        if (params->ka_num != 10) {
            return Error_IncorrectNumberOfAccounts;
        }
        SolAccountInfo *config_account = &(params->ka[4]);
        SolAccountInfo *admin_account = &(params->ka[5]);
        SolAccountInfo *entry_token_account = &(params->ka[6]);
        SolAccountInfo *authority_account = &(params->ka[7]);
        SolAccountInfo *token_destination_account = &(params->ka[8]);
        // The account at index 8 must be the SPL-Token program, but this is not checked; if it's not that program,
        // then the transaction will simply fail when the token is transferred

        // Check permissions
        if (!entry_account->is_writable) {
            return Error_InvalidAccountPermissions_First + 2;
        }
        if (!admin_account->is_writable) {
            return Error_InvalidAccountPermissions_First + 5;
        }
        if (!entry_token_account->is_writable) {
            return Error_InvalidAccountPermissions_First + 6;
        }
        if (!token_destination_account->is_writable) {
            return Error_InvalidAccountPermissions_First + 7;
        }

        // Check to make sure that the auction is over
        if (is_entry_in_normal_auction(block, entry, &clock)) {
            return Error_EntryInAuction;
        }

        // Check to make sure that the admin account is the correct account
        if (!is_admin_account(config_account, admin_account->key)) {
            return Error_InvalidAccount_First + 5;
        }
        
        // Check to make sure that the entry_token_account is the correct account for this entry
        if (!SolPubkey_same(entry_token_account->key, &(entry->token_account.address))) {
            return Error_InvalidAccount_First + 6;
        }

        // Check to make sure that the authority account is the correct account
        if (!is_nifty_authority_account(authority_account->key)) {
            return Error_InvalidAccount_First + 7;
        }

        // Transfer the entry token (NFT) to the destination account.  This will fail if the winning bid was
        // already claimed.
        uint64_t ret = transfer_entry_token(block, entry, token_destination_account, params->ka, params->ka_num);
        if (ret) {
            return ret;
        }

        // OK transferred the token, so move the bid account lamports to the admin
        *(admin_account->lamports) += *(bid_account->lamports);
        *(bid_account->lamports) = 0;
    }
    else {
        // Claim of a losing bid; just move the bid account lamports to the bidding account, which must be writable
        if (!bidding_account->is_writable) {
            return Error_InvalidAccountPermissions_First;
        }
        
        *(bidding_account->lamports) += *(bid_account->lamports);
        *(bid_account->lamports) = 0;
    }
    
    return 0;
}
