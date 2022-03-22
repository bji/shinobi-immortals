
// To be used as data to pass to the system program when invoking CreateAccount
typedef struct __attribute__((__packed__))
{
    uint32_t instruction_code;
    uint64_t lamports;
    uint64_t space;
    SolPubkey owner;
} CreateAccountData;


// Creates a new account, using sol_invoke_signed, which is what allows the newly created account to take ownership
// of the created account
static uint64_t create_pda(SolAccountInfo *new_account, SolSignerSeed *seeds, int seeds_count,
                           SolAccountInfo *funding_account, SolPubkey *owner_account,
                           uint64_t funding_lamports, uint64_t space,
                           // All cross-program invocation must pass all account infos through, it's
                           // the only sane way to cross-program invoke
                           SolAccountInfo *all_accounts, int all_accounts_len)
{
    SolInstruction instruction;

    SolAccountMeta account_metas[2] = 
        // First account to pass to CreateAccount is the funding_account
        { { /* pubkey */ funding_account->key, /* is_writable */ true, /* is_signer */ true },
          // Second account to pass to CreateAccount is the new account to be created
          { /* pubkey */ new_account->key, /* is_writable */ true, /* is_signer */ true } };

    CreateAccountData data = { 0, funding_lamports, space, *owner_account };

    SolPubkey system_program_id = SYSTEM_PROGRAM_ID_BYTES;
    instruction.program_id = &system_program_id;
    instruction.accounts = account_metas;
    instruction.account_len = 2;
    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    SolSignerSeeds signer_seeds = { seeds, seeds_count };

    return sol_invoke_signed(&instruction, all_accounts, all_accounts_len, &signer_seeds, 1);
}
