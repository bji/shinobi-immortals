
// Transaction must be signed by the admin address contained within the Admin Identifier Account.

// Account references:
// 0. `[]` Program config account -- this must be g_program_config_account_address
// 1. `[SIGNER]` -- This must be the admin account
// 2. `[WRITE, SIGNER]` Funding account
// 3. `[]` The system account, so that cross-program invoke of the system account can be done

// instruction data type for CreateBlock instruction.
typedef struct CreateBlockData
{
    // This is the instruction code for CreateBlock
    uint8_t instruction_code;
    
    // The Program Derived Account address is generated from two 32 bit numbers.  The first is a block group id.  The
    // second is a block id within that group.  These two numbers together (in their little endian encoding) form the
    // seed that is used to generate the address of the block.  If the PDA that is derived from these seeds is
    // invalid, the transaction fails.
    uint32_t group_id;
    uint32_t block_id;
    
    // Number of lamports to transfer from the funding account to the newly created account.  Sufficient lamports must
    // be supplied to make the account rent exempt, or else the transaction will fail.  It is not possible to withdraw
    // lamports from the funding account, except when the account is deleted on a successful DeleteBlock instruction.
    uint64_t funding_lamports;

    // Total number of entries in this block.  Must be greater than 0.
    uint16_t total_entry_count;

    // Number of tickets which when purchased will allow a reveal.  Must be equal to or less than total_entry_count.
    uint16_t reveal_ticket_count;

    // If this value is true, then the ticket_phase_duration value specifies a number of slots to add to the current
    // slot number (from syssvar SlotHistory) to get the "reveal time".  If this value is false, then
    // ticket_phase_duration specifies a number of seconds to add to the current time (from sysvar Clock) to get the
    // "reveal time".
    bool ticket_phase_duration_is_slots;

    // This is either a number of slots (if ticket_phase_duration_is_slots is true) or a number of seconds (if
    // ticket_phase_duration_is_slots is false) to add to the current slot/time to get the reveal slot/time.
    uint32_t ticket_phase_duration;

    // This is cost in lamports of each ticket
    uint64_t ticket_price_lamports;

    // This is the number of lamports to return to the user when a ticket is refunded.  It must be less than or
    // equal to ticket_price_lamports.
    uint64_t ticket_refund_lamports;
    
    // If this value is true, then the auction_duration value specifies a number of slots to add to the auction
    // begin slot number (from syssvar SlotHistory) to get end of auction.  If this value is false, then
    // auction_duration specifies a number of seconds to add to the current time (from sysvar Clock) to get the
    // end of auction.
    bool auction_duration_is_slots;

    // This is either a number of slots (if auction_duration_is_slots is true) or a number of seconds (if
    // auction_duration_is_slots is false) to add to the current slot/time to get the end of auction time.
    uint32_t auction_duration;

    // This is the minimum number of lamports for any auction bid.  This is also the sale price of an entry whose
    // auction has completed without any bids.
    uint64_t bid_minimum;

    // This is a 16 bit fixed point value giving the commission to charge on stake reward earned by stake accounts
    // held by the program in this account, with 7 bits of 'whole value' and 9 bits of fractional value.
    fp16_t commission;

    // This is the minimum number of lamports commission to charge for an NFT.  Which means that when an NFT is
    // returned, if the total commission charged while the user owned the NFT is less than
    // minimum_accrued_commission_lamports, then commission will be charged to make up the difference.
    uint64_t minimum_accrued_commission_lamports;

    // This is the fraction of the stake account balance to use as the "permabuy" price.  If the user has a purchased
    // an entry, they can get their stake account back while retaining the entry by paying this "permabuy" commission.
    // Then the entry is basically owned outright by the user without any stake account committment.  At this
    // point the entry is completely owned and cannot be modified anymore.
    fp16_t permabuy_commission;

    // This is the extra cost in lamports charged when a stake account that is not delegated to Shinobi Systems
    // is un-delegated via the redelegation crank.
    uint64_t undelegate_charge_lamports;

    // This is the extra cost in lamports charged when a stake account that is undelegated is delegated to
    // Shinobi Systems via the redelegation crank.
    uint64_t delegate_charge_lamports;
    
    // This is the number of Ki per lamport to award for each staked epoch for entries in this block
    uint32_t ki_earned_per_staked_lamport;
    
} CreateBlockData;


// Creates a new block of entries
static uint64_t do_create_block(SolParameters *params)
{
    // Sanitize the accounts.  There must be 4.
    if (params->ka_num != 4) {
        return Error_IncorrectNumberOfAccounts;
    }

    SolAccountInfo *config_account = &(params->ka[0]);
    SolAccountInfo *admin_account = &(params->ka[1]);
    SolAccountInfo *funding_account = &(params->ka[2]);
    SolAccountInfo *system_account = &(params->ka[3]);
    
    // Ensure the the caller is the admin user
    if (!is_admin_authenticated(config_account, admin_account)) {
        return Error_PermissionDenied;
    }

    // Ensure funding account is a signer and is writable, but not executable
    if (!funding_account->is_signer || !funding_account->is_writable || funding_account->executable) {
        return Error_InvalidFundingAccount;
    }

    // Ensure that the system account reference was for the actual system account and that it has the correct flags
    if (!is_system_program(system_account->key)) {
        return Error_InvalidSystemAccount;
    }

    // Ensure that the instruction data is the correct size
    if (params->data_len != sizeof(CreateBlockData)) {
        return Error_ShortData;
    }

    // Cast to instruction data
    CreateBlockData *data = (CreateBlockData *) params->data;

    // Compute the address of the block
    SolPubkey block_address;
    // Two seeds: the group id and the block id
    SolSignerSeed seeds[2] =
        { // First is group id
            { (uint8_t *) &(data->group_id), 4 },
            // Second is block id
            { (uint8_t *) &(data->block_id), 4 }
        };
    SolPubkey nifty_stakes_program_id = NIFTY_STAKES_PROGRAM_ID_BYTES;
    if (!sol_create_program_address(seeds, 2, &nifty_stakes_program_id, &block_address)) {
        return Error_InvalidBlockAddressSeeds;
    }

    // Number of create the account with???  Should be sizeof(SomeDataStructure) +
    // (total_entry_count * sizeof(SomeOtherDataStructure))
    uint64_t space = 100;
    
    if (!create_account_with_seeds(&block_address, seeds, 2, funding_account, (SolPubkey *) params->program_id,
                                   data->funding_lamports, space)) {
        return Error_CreateAccountFailed;
    }

    // The block account has been created, now store data into it
    

    // The program has succeeded.  Return success code 0.
    return 0;
}
