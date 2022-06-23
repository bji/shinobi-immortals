
// XXX two states in which buy is possible
//
// a. PreRevealUnowned
//    Preconditions:
//      -> Block not revealable yet
//    Price:
//      -> pct = (now - block->block_start_timestamp) / block->mystery_phase_duration
//      -> price = block->minimum_bid_lamports + (pct * (block->initial_mystery_price_lamports -
//                                                       block->minimum_bid_lamports))
//    -> Lamports are moved from funding account to nifty program holding account
//    -> Token is moved to destination account
//    Resulting state:
//      -> PreRevealOwned
//      -> mysteries_sold_count++
//      -> If mysteries_sold_count == block->total_mystery_count, reveal_period_start_timestamp is set

// b. Unsold (if not in auction)
//    -> Price is minimum bid price
//    -> Lamports are moved from funding account to admin account



// Account references:
// 0. `[SIGNER]` -- The funding account to pay all fees associated with this transaction
// 1. `[]` -- The SPL token program, for cross-program invoke
// 2. `[WRITE]` -- The block account address
// 3. `[WRITE]` -- The entry account address
// 4. `[WRITE]` -- The entry token account (must be a PDA of the nifty program, derived from ticket_token_seed from
//                 instruction data)
// 5. `[WRITE]` -- The account to give the purchase funds to (for purchase of pre-release entry, this must be the
//                 nifty program authority account; for purchse of post-release entry, this must be the admin account)
// 6. `[WRITE]` -- The account to give ownership of the entry to
// OPTIONAL: 7. `[]` -- The config account, only needed if the account to give ownership to is the admin account

typedef struct
{
    // This is the instruction code for Buy
    uint8_t instruction_code;
    
} BuyData;


static uint64_t user_buy(SolParameters *params)
{
    #if 0
    // Sanitize the accounts.  There must be 7 or 8.
    if ((params->ka_num != 7) && (params->ka_num != 8)) {
        return Error_IncorrectNumberOfAccounts;
    }

    SolAccountInfo *funding_account = &(params->ka[0]);
    // The 2nd account must be the SPL token program, but this is not checked; if it's not that program, then the
    // transaction will simply fail when it is invoked later
    SolAccountInfo *block_account = &(params->ka[2]);
    SolAccountInfo *entry_account = &(params->ka[3]);
    SolAccountInfo *entry_token_account = &(params->ka[4]);
    SolAccountInfo *funds_destination_account = &(params->ka[5]);
    SolAccountInfo *token_destination_account = &(params->ka[6]);

    // Make sure that the input data is the correct size
    if (params->data_len != sizeof(BuyData)) {
        return Error_InvalidDataSize;
    }

    // Cast to instruction data
    BuyData *data = (BuyData *) params->data;

    // Check permissions
    if (!funding_account->is_writable || !funding_account->is_signer) {
        return Error_InvalidAccountPermissions_First;
    }
    if (!block_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 2;
    }
    if (!entry_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 3;
    }
    if (!entry_token_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 4;
    }
    if (!funds_destination_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 5;
    }
    if (!token_destination_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 6;
    }

    // This is the block data
    Block *block = get_validated_block(block_account);
    if (!block) {
        return Error_InvalidAccount_First + 2;
    }

    // This is the entry data
    Entry *entry = get_validated_entry(block, entry_account);
    if (!entry) {
        return Error_InvalidAccount_First + 3;
    }

    // Check that the correct token account address is supplied
    if (!SolPubkey_same(entry_token_account->key, &(entry->token_account))) {
        return Error_InvalidAccount_First + 4;
    }

    // Ensure that the block is complete; cannot buy anything from a block that is not complete yet
    if (!is_block_complete(block)) {
        return Error_BlockNotComplete;
    }

    // Get the clock sysvar, needed below
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }

    // No need to check that the token is owned by the nifty stakes program; if it is not, then the SPL token program
    // invoke of the transfer of the token from the token account to the destination account will simply fail and the
    // transaction will then fail

    uint64_t purchase_price_lamports;

    switch (get_entry_state(block, entry, clock)) {
    case EntryState_PreRevealOwned:
    case EntryState_WaitingForRevealOwned:
    case EntryState_Owned:
    case EntryState_OwnedAndStaked:
        // Already owned, can't be purchased again
        return Error_AlreadyPurchased;

    case EntryState_WaitingForRevealUnowned:
        // In reveal grace period waiting for reveal, can't be purchased
        return Error_BlockInRevealGracePeriod;

    case EntryState_InAuction:
        // In auction, can't be purchased
        return Error_EntryInAuction;

    case EntryState_WaitingToBeClaimed:
        // Has a winning auction bid, can't be purchased
        return Error_EntryWaitingToBeClaimed;
        
    case EntryState_PreRevealUnowned: {
        // Pre-reveal but not owned yet.  Can be purchased.
        
        // Ensure that the funds destination account is the authority account
        if (!is_nifty_authority_account(funds_destination_account->key)) {
            return Error_InvalidAccount_First + 7;
        }

        // Compute price.  It is a linear interpolation over the time range of the mystery period, between the
        // initial mystery price, and the minimum bid price.
        uint64_t elapsed_duration = clock.unix_timestamp - block->block_start_timestamp;
        uint64_t price_spread = block->config.initial_mystery_price_lamports - block->config.minimum_bid_lamports;
            
        purchase_price_lamports = (block->config.minimum_bid_lamports + 
                                   ((price_spread / ((uint64_t) block->mystery_phase_duration)) * elapsed_duration));

        break;
    }

    case EntryState_Unowned: {
        // Revealed but didn't sell at auction.  Can be purchased.

        // Ensure that the funds destination account is the admin account
        SolPubkey admin_account_address;
        if ((params->ka_num != 8) ||
            !get_admin_account_address(&(params->ka[7]), &admin_account_address) ||
            !SolPubkey_same(funds_destination_account->key, &admin_account_address)) {
            return Error_InvalidAccount_First + 7;
        }

        // Price is the minimum bid.
        purchase_price_lamports = block->config.minimum_bid_lamports;
        
        break;
    }
    }

    // Check to ensure that the funds source has at least the purchase price lamports
    if (*(funding_account->lamports) < purchase_price_lamports) {
        return Error_InsufficientFunds;
    }

    // Transfer the purchase price from the funds source to the funds destination account
    (*(funding_account->lamports)) -= purchase_price_lamports;

    (*(funds_destination_account->lamports)) += purchase_price_lamports;

    // Create the destination token account if it doesn't exist already
    if (*(token_destination_account->lamports) == 0) {
        
    }

    // Transfer the token to the token destination account.  If the mints don't match, this will just fail
    SolInstruction instruction;

    SolAccountMeta account_metas[] =
        { { 
          { destination_account },
          { source_account_owner }
        };


    sol_invoke_signed();

#if 0
    
    // Ensure that the stake account does not have a lockup period, because it will not be possible to collect
    // commission on such an account
    if ((stake_account_data->lockup_unix_timestamp > clock.unix_timestamp) ||
        (stake_account_data->lockup_epoch > clock.epoch)) {
        return Error_InvalidStakeAccount;
    }

    // Create a ticket mint

    // Mint a ticket

    // Create ticket metadata

    // Set the ticket address into the entry

    // Cross-program invoke the stake program to re-assign all authorities of the stake account to the nifty program

#endif

#endif
    
    // Not done yet ...
    return 2000;
}


static uint64_t do_buy_prerevealunowned(SolAccountInfo *funding_account, SolAccountInfo *block_account,
                                        SolAccountInfo *entry_account, SolAccountInfo *entry_token_account,
                                        SolAccountInfo *escrow_account, SolAccountInfo *destination_account,
                                        Block *block, Entry *entry, uint8_t escrow_account_bump_seed, Clock *clock)
{
    // Check block to make sure that it's not already past the prereveal phase
    if (is_complete_block_revealable(block, clock)) {
        return 2001;
    }

    // Compute the price.  It is a linear interpolation between the block's initial_mystery_price_lamports and
    // the minimum_bid_lamports, interpolated over the time duration of the mystery phase.
    uint64_t price_range = block->config.initial_mystery_price_lamports - block->config.minimum_bid_lamports;

    uint64_t mystery_phase_completed = clock->unix_timestamp - block->state.reveal_period_start_timestamp;

    uint64_t price = (block->config.initial_mystery_price_lamports -
                      ((price_range * mystery_phase_completed) / block->config.mystery_phase_duration));

    return 2000;
}


static uint64_t do_buy_unsold(SolAccountInfo *funding_account, SolAccountInfo *block_account,
                              SolAccountInfo *entry_account, SolAccountInfo *entry_token_account,
                              SolAccountInfo *escrow_account, SolAccountInfo *destination_account,
                              Block *block, Entry *entry, Clock *clock)
{
    return 2000;
}
