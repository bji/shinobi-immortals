
// To be used as data to pass to the system program when invoking CreateAccount
typedef struct
{
    uint64_t lamports;
    uint64_t space;
    SolPubkey owner;
} CreateAccountWithSeedsData;


// Creates a new account, using sol_invoke_signed, which is what allows the newly created account to take ownership
// of the created account
static uint64_t create_account_with_seeds(SolPubkey *account_pubkey, SolSignerSeed *seeds, int seeds_count,
                                          SolAccountInfo *funding_account, SolPubkey *owner_account,
                                          uint64_t funding_lamports, uint64_t space)
{
    SolInstruction instruction;
    
    SolAccountMeta account_metas[2] = 
        // First account to pass to CreateAccount is the funding_account
        { { /* pubkey */ funding_account->key, /* is_writable */ true, /* is_signer */ false },
          // Second account to pass to CreateAccount is the new account to be created
          { /* pubkey */ account_pubkey, /* is_writable */ true, /* is_signer */ false } };
    
    // No account_info for the second account because it's not an existing account yet
    SolAccountInfo account_info;;
    sol_memcpy(&account_info, funding_account, sizeof(SolAccountInfo));

    CreateAccountWithSeedsData data = { funding_lamports, space, *owner_account };

    SolPubkey system_program_id = SYSTEM_PROGRAM_ID_BYTES;
    instruction.program_id = &system_program_id;
    instruction.accounts = account_metas;
    instruction.account_len = 2;
    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    SolSignerSeeds signer_seeds = { seeds, seeds_count };

    return sol_invoke_signed(&instruction, &account_info, 1, &signer_seeds, 1);
}
