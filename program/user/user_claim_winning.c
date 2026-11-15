#pragma once


static uint64_t user_claim_winning(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,   bidding_account,                  ReadWrite,  Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(1,   entry_account,                    ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,   bid_account,                      ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(3,   state_account,                    ReadOnly,   NotSigner,  KnownAccount_ProgramState);
        DECLARE_ACCOUNT(4,   admin_account,                    ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(5,   entry_token_account,              ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(6,   entry_mint_account,               ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(7,   authority_account,                ReadOnly,   NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(8,   token_destination_account,        ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(9,   token_destination_owner_account,  ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(10,  system_program_account,           ReadOnly,   NotSigner,  KnownAccount_SystemProgram);
        DECLARE_ACCOUNT(11,  spl_token_program_account,        ReadOnly,   NotSigner,  KnownAccount_SPLTokenProgram);
        DECLARE_ACCOUNT(12,  spl_ata_program_account,          ReadOnly,   NotSigner,  KnownAccount_SPLATAProgram);
        DECLARE_ACCOUNT(13,  entry_metadata_account,           ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(14,  metaplex_program_account,         ReadOnly,   NotSigner,  KnownAccount_MetaplexProgram);
    }

    // If there are more than 15 accounts, then the optional reclaiming of bid marker is requested
    bool reclaim_bid_marker = (params->ka_num > 15);

    // If reclaiming bid marker, there must be 17 accounts
    if (reclaim_bid_marker) {
        DECLARE_ACCOUNTS_NUMBER(17);
    }
    // Else there must be 15
    else {
        DECLARE_ACCOUNTS_NUMBER(15);
    }

    // This instruction takes no data beyond the instruction code byte, so ensure that no data was supplied
    if (params->data_len != 1) {
        return Error_InvalidDataSize;
    }

    // This is the entry data
    Entry *entry = get_validated_entry(entry_account);
    if (!entry) {
        return Error_InvalidAccount_First + 1;
    }

    // Ensure that the correct admin account was passed in
    if (!is_admin_account(state_account, admin_account->key)) {
        return Error_InvalidAccount_First + 4;
    }

    // Check to make sure that the entry_token_account is the correct account for this entry
    if (!SolPubkey_same(entry_token_account->key, &(entry->token_pubkey))) {
        return Error_InvalidAccount_First + 5;
    }

    // Check to make sure that the entry_mint_account is the correct account for this entry
    if (!SolPubkey_same(entry_mint_account->key, &(entry->mint_pubkey))) {
        return Error_InvalidAccount_First + 6;
    }

    // Check to make sure that the entry_metadata_account is the Metaplex metadata account of this entry
    if (!SolPubkey_same(entry_metadata_account->key, &(entry->metaplex_metadata_pubkey))) {
        return Error_InvalidAccount_First + 13;
    }

    // Get the clock sysvar, needed below
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }

    // The only time that a winning bid could possibly be claimed is if the entry is in the WaitingToBeClaimed state
    if (get_entry_state(0, entry, &clock) != EntryState_WaitingToBeClaimed) {
        return Error_CannotClaimBid;
    }

    // Get the validated bid account data
    const Bid *bid = get_validated_bid(bid_account);
    if (!bid) {
        return Error_InvalidAccount_First + 2;
    }

    // If this is the not the winning bid pubkey, then it cannot claim the winning bid
    if (!SolPubkey_same(bid_account->key, &(entry->auction.winning_bid_pubkey))) {
        return Error_CannotClaimBid;
    }

    // If the bidder pubkey written into the bid is not the same as the bidder that was provided in this transaction,
    // then this is an invalid attempt to claim a bid
    if (!SolPubkey_same(bidding_account->key, &(bid->bidder_pubkey))) {
        return Error_InvalidAccount_First;
    }

    // Ensure that the token destination account exists
    uint64_t ret = create_associated_token_account_idempotent(token_destination_account, &(entry->mint_pubkey),
                                                              token_destination_owner_account->key,
                                                              bidding_account->key, params->ka, params->ka_num);
    if (ret) {
        return ret;
    }

    // Transfer the entry token to the destination account
    ret = transfer_entry_token(entry, token_destination_account, params->ka, params->ka_num);
    if (ret) {
        return ret;
    }

    // If the accounts were provided that would allow the bid marker token account to be reclaimed, do so
    if (reclaim_bid_marker) {
        {
            DECLARE_ACCOUNT(15,   bid_marker_mint_account,          ReadWrite,  NotSigner,  KnownAccount_NotKnown);
            DECLARE_ACCOUNT(16,   bid_marker_token_account,         ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        }

        // Burn the bid marker tokens
        uint64_t ret = reclaim_bid_marker_token(&(entry->mint_pubkey), bidding_account, bid_marker_mint_account,
                                                bid_marker_token_account, params->ka, params->ka_num);
        if (ret) {
            return ret;
        }
    }

    // Set the purchase price on the entry to the winning bid amount, so that the entry now goes into an Owned
    // state
    entry->purchase_price_lamports = *(bid_account->lamports);

    // Set the primary_sale_happened flag on the metaplex metadata, as user_buy does, which isn't strictly necessary
    // but just in case there are UI presentations that care
    ret = set_metaplex_metadata_primary_sale_happened(entry, params->ka, params->ka_num);
    if (ret) {
        return ret;
    }

    // Close the entry's token account, as user_buy does, since it is now empty and will never be used again.  Its
    // lamports go to the admin account.
    ret = close_entry_token(entry, admin_account->key, params->ka, params->ka_num);
    if (ret) {
        return ret;
    }

    // Finally, delete the bid account, moving its lamports to the admin.  This is done last, after all cross-program
    // invocations, so that none of them sees the bid account in its deleted state.
    delete_account(bid_account, admin_account);

    return 0;
}
