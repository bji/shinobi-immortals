
// Transaction must be signed by the admin address contained within the Admin Identifier Account.

// Account references:
// 0. `[]` Program config account -- this must be g_program_config_account_address
// 1. `[SIGNER]` -- This must be the admin account
// 2. `[WRITE]` -- Destination for lamports recovered from deleted block
// 4. `[WRITE]` -- The block account address (this is the address that results from finding a program address
//                 using the group id and block id as seeds)

static uint64_t do_delete_block(SolParameters *params)
{
    // Sanitize the accounts.  There must be 4.
    if (params->ka_num != 4) {
        return Error_IncorrectNumberOfAccounts;
    }

    SolAccountInfo *config_account = &(params->ka[0]);
    SolAccountInfo *admin_account = &(params->ka[1]);
    SolAccountInfo *destination_account = &(params->ka[2]);
    SolAccountInfo *block_account = &(params->ka[3]);

    // Ensure the the transaction has been authenticated by the admin
    if (!is_admin_authenticated(config_account, admin_account)) {
        return Error_PermissionDenied;
    }

    // Ensure that the destination account can be written to
    if (!destination_account->is_writable) {
        return Error_InvalidDestinationAccount;
    }

    // Ensure that the block account is writable too.  Doesn't need to be a signer since the program can write it
    // because it's the owner.
    if (!block_account->is_writable) {
        return Error_BadPermissions;
    }

    // Now check to make sure that the number entries that have been added is less than the total number of
    // entries in the block, which means that this is an incomplete block.  Only incomplete blocks can be
    // deleted.

    BlockData *data = (BlockData *) block_account->data;

    if (data->state.added_entries_count == data->config.total_entry_count) {
        return Error_CompletedBlockCannotBeDeleted;
    }

    // Delete the block.  This is accomplished by moving all of its lamports out.

    *(destination_account->lamports) += *(block_account->lamports);

    *(block_account->lamports) = 0;
    
    return 0;
}
