
// Account references:
// 0. `[WRITE]` -- The funding account to pay all fees associated with this transaction
// 1. `[WRITE]` -- The block account address
// 2. `[WRITE]` -- The stake account
// 3. `[SIGNER]` -- The stake account withdraw authority
// 4. `[WRITE]` -- The ticket mint account (must be a PDA of the nifty program, derived from ticket_mint_seed from
//                 instruction data)
// 5. `[WRITE]` -- The ticket token account (must be a PDA of the nifty program, derived from ticket_token_seed from
//                 instruction data)
// 6. `[WRITE]` -- The account to give ownership of the ticket to
// 7. `[]` -- The metaplex metadata account (for cross-program invoke)
// 8. `[]` -- The stake account program (for cross-program invoke)


typedef struct
{
    // This is the instruction code for BuyTicket
    uint8_t instruction_code;
    
    // Seed to use to create the ticket mint account
    uint8_t ticket_mint_seed[32];

    // Seed to use to create the ticket token account
    uint8_t ticket_token_seed[32];
    
    // Index within the block of the entry to buy
    uint16_t entry_index;
    
} BuyTicketData;


static uint64_t do_buy_ticket(SolParameters *params)
{
    // Sanitize the accounts.  There must be 8.
    if (params->ka_num != 8) {
        return Error_IncorrectNumberOfAccounts;
    }

    SolAccountInfo *funding_account = &(params->ka[0]);
    SolAccountInfo *block_account = &(params->ka[1]);
    SolAccountInfo *stake_account = &(params->ka[2]);
    SolAccountInfo *stake_withdraw_authority = &(params->ka[3]);
    SolAccountInfo *ticket_mint_account = &(params->ka[4]);
    SolAccountInfo *ticket_token_account = &(params->ka[5]);
    SolAccountInfo *ticket_destination_account = &(params->ka[6]);
    // 7 is the metaplex metadata account, but doesn't need to be checked because it will simply fail on
    // cross-program invocation if it's not in the account list
    // 8 is the stake account program, but doesn't need to be checked because it will simply fail on
    // cross-program invocation if it's not in the account list

    // Make sure that the input data is the correct size
    if (params->data_len != sizeof(BuyTicketData)) {
        return Error_InvalidDataSize;
    }

    // Cast to instruction data
    BuyTicketData *data = (BuyTicketData *) params->data;

    // Ensure that the block account is writable too.  Doesn't need to be a signer since the program can write it
    // because it's the owner.
    if (!block_account->is_writable) {
        return Error_BadPermissions;
    }
    
    // Ensure that the block account is owned by the program
    if (!is_nifty_stakes_program(block_account->owner)) {
        return Error_InvalidAccount_First + 2;
    }

    // This is the block data
    Block *block = (Block *) block_account->data;

    // If the block does not have the correct data type, then this is an error (admin accidentally treating a bid as a
    // block?)
    if (block->data_type != DataType_Block) {
        return Error_IncorrectAccountType;
    }

    // Ensure that the block is complete; cannot buy ticket of a block that is not complete yet
    if (!is_block_complete(block)) {
        return Error_BlockNotComplete;
    }

    // Ensure that the entry index is valid
    if (data->entry_index >= block->config.total_entry_count) {
        return Error_InvalidData_First;
    }

    // This is the entry itself
    BlockEntry *entry = &(block->entries[data->entry_index]);

    // Ensure that the entry is pre-reveal
    if (entry->state != BlockEntryState_PreReveal) {
        // Cannot buy ticket for an entry which has already been revealed
        return Error_AlreadyRevealed;
    }

    // Ensure that the entry doesn't already have a bought ticket
    if (!is_empty_pubkey(&(entry->ticket_or_bid_account))) {
        return Error_TicketAlreadyPurchased;
    }

    // Assume that the passed-in stake account is a legit stake account with the withdraw authority set to
    // the stake_withdraw_authority account.  If this is not true, then the cross-program invoke of the stake
    // program will fail, which will appropriately abort the transaction.

    // Ensure that the passed-in stake account has the correct number of lamports.  It must be exactly the ticket
    // price.
    if (*stake_account->lamports != block->config.ticket_price_lamports) {
        return Error_IncorrectStakeAccountBalance;
    }

    // The passed-in stake account must be the correct size to be a stake account
    if (stake_account->data_len != sizeof(StakeAccountData)) {
        return Error_InvalidStakeAccount;
    }

    StakeAccountData *stake_account_data = (StakeAccountData *) stake_account->data;

    // Ensure that the stake account is in the initialized or stake state
    if ((stake_account_data->state != StakeState_Initialized) && (stake_account_data->state != StakeState_Stake)) {
        return Error_InvalidStakeAccount;
    }

    // Ensure that the nifty stake program isn't already the withdraw authority of this stake account
    if (is_nifty_stakes_program(&(stake_account_data->authorized_withdrawer))) {
        return Error_InvalidStakeAccount;
    }

    // Get the clock sysvar, needed below
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }
    
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

    // Not done yet ...
    return 2000;
}
