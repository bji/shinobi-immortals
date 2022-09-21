#pragma once


static uint64_t mint_bid_marker_token_idempotent(SolAccountInfo *bid_marker_token_account, SolPubkey *entry_mint_key,
                                                 SolPubkey *bidder_key, SolAccountInfo *transaction_accounts,
                                                 int transaction_accounts_len)
{
    // Compute the bid marker token address
    uint8_t prefix = PDA_Account_Seed_Prefix_Bid_Marker_Token;

    uint8_t bump_seed;

    SolSignerSeed seeds[] = { { &prefix, sizeof(prefix) },
                              { (uint8_t *) entry_mint_key, sizeof(*entry_mint_key) },
                              { (uint8_t *) bidder_key, sizeof(*bidder_key) },
                              { &bump_seed, sizeof(bump_seed) } };

    SolPubkey pubkey;
    uint64_t ret = sol_try_find_program_address(seeds, ARRAY_LEN(seeds) - 1, &(Constants.nifty_program_pubkey),
                                                &pubkey, &bump_seed);
    if (ret) {
        return ret;
    }

    // Verify that the bid marker token address is as expected
    if (!SolPubkey_same(&pubkey, bid_marker_token_account->key)) {
        return Error_CreateAccountFailed;
    }

    // Only if the token doesn't already exist should it be created
    if (!is_token_account(bid_marker_token_account, &(Constants.bid_marker_mint_pubkey), 0)) {
        // Create the bid marker token account
        ret = create_pda_token_account(bid_marker_token_account, seeds, ARRAY_LEN(seeds),
                                       &(Constants.bid_marker_mint_pubkey), /* owner */ bidder_key,
                                       transaction_accounts, transaction_accounts_len);
        if (ret) {
            return ret;
        }
    }

    // Mint a token into it, to prevent the user from cleaning it up because "it's empty".  Must mint 10 because the
    // tokens are stored on-chain as "decitokens", to comply with the metaplex fungible token metadata standard.
    return mint_tokens(&(Constants.bid_marker_mint_pubkey), bid_marker_token_account->key, 10,
                       transaction_accounts, transaction_accounts_len);
}


static uint64_t create_entry_bid_account(SolAccountInfo *bid_account, SolPubkey *bid_marker_key, SolPubkey *mint_key,
                                         SolPubkey *bidder_key, uint64_t bid_lamports,
                                         SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    // Compute the bid address
    uint8_t prefix = PDA_Account_Seed_Prefix_Bid;

    uint8_t bump_seed;

    SolSignerSeed seeds[] = { { &prefix, sizeof(prefix) },
                              { (uint8_t *) bid_marker_key, sizeof(*bid_marker_key) },
                              { &bump_seed, sizeof(bump_seed) } };

    SolPubkey pubkey;
    uint64_t ret = sol_try_find_program_address(seeds, ARRAY_LEN(seeds) - 1, &(Constants.nifty_program_pubkey),
                                                &pubkey, &bump_seed);
    if (ret) {
        return ret;
    }

    // Verify that the bid account address is as expected
    if (!SolPubkey_same(&pubkey, bid_account->key)) {
        return Error_CreateAccountFailed;
    }

    ret = create_pda(bid_account, seeds, ARRAY_LEN(seeds), bidder_key, &(Constants.nifty_program_pubkey),
                     bid_lamports, sizeof(Bid), transaction_accounts, transaction_accounts_len);
    if (ret) {
        return ret;
    }

    // Now set the contants of the Bid account
    Bid *bid = (Bid *) bid_account->data;

    bid->data_type = DataType_Bid;

    bid->mint_pubkey = *mint_key;

    bid->bidder_pubkey = *bidder_key;

    return 0;
}


static uint64_t reclaim_bid_marker_token(SolPubkey *entry_token_mint_pubkey,
                                         SolAccountInfo *bidding_account,
                                         SolAccountInfo *bid_marker_mint_account,
                                         SolAccountInfo *bid_marker_token_account,
                                         SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    if (!bidding_account->is_writable) {
        return Error_FailedToReclaimBidMarkerToken;
    }
    if (!bid_marker_mint_account->is_writable) {
        return Error_FailedToReclaimBidMarkerToken;
    }
    if (!bid_marker_token_account->is_writable) {
        return Error_FailedToReclaimBidMarkerToken;
    }

    // Ensure that the correct bid marker mint account was passed in
    if (!is_bid_marker_mint_account(bid_marker_mint_account->key)) {
        return Error_FailedToReclaimBidMarkerToken;
    }

    // Compute the bid marker token address
    uint8_t prefix = PDA_Account_Seed_Prefix_Bid_Marker_Token;

    uint8_t bump_seed;

    SolSignerSeed seeds[] = { { &prefix, sizeof(prefix) },
                              { (uint8_t *) entry_token_mint_pubkey, sizeof(*entry_token_mint_pubkey) },
                              { (uint8_t *) bidding_account->key, sizeof(*(bidding_account->key)) },
                              { &bump_seed, sizeof(bump_seed) } };

    SolPubkey pubkey;
    uint64_t ret = sol_try_find_program_address(seeds, ARRAY_LEN(seeds) - 1, &(Constants.nifty_program_pubkey),
                                                &pubkey, &bump_seed);
    if (ret) {
        return ret;
    }

    // Verify that the entry token address is as expected
    if (!SolPubkey_same(&pubkey, bid_marker_token_account->key)) {
        return Error_FailedToReclaimBidMarkerToken;
    }

    // Figure out how many tokens are in it
    uint64_t token_amount = ((SolanaTokenProgramTokenData *) bid_marker_token_account->data)->amount;

    // If there are tokens in there, burn them
    if (token_amount > 0) {
        uint64_t ret = burn_tokens(bid_marker_token_account->key, bidding_account->key,
                                   &(Constants.bid_marker_mint_pubkey), token_amount,
                                   transaction_accounts, transaction_accounts_len);
        if (ret) {
            return ret;
        }
    }

    return close_token_account(bid_marker_token_account->key, bidding_account->key, bidding_account->key,
                               transaction_accounts, transaction_accounts_len);
}


// Given a bid account, returns the validated Bid or null if the entry account is invalid in some way.
static Bid *get_validated_bid(SolAccountInfo *bid_account)
{
    // Make sure that the bid account is owned by the nifty stakes program
    if (!is_nifty_program(bid_account->owner)) {
        return 0;
    }

    // Entry account must be the correct size
    if (bid_account->data_len != sizeof(Bid)) {
        return 0;
    }

    Bid *bid = (Bid *) bid_account->data;

    // If the bid does not have the correct data type, then it's not a bid
    if (bid->data_type != DataType_Bid) {
        return 0;
    }

    return bid;
}
