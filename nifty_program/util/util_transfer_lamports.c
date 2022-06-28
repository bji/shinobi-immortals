
#ifndef UTIL_TRANSFER_LAMPORTS_C
#define UTIL_TRANSFER_LAMPORTS_C


typedef struct __attribute__((packed))
{
    uint32_t instruction_code; // 2 for Transfer
    
    uint64_t amount;
} util_SystemTransferData;


static uint64_t util_transfer_lamports(SolPubkey *source_account, SolPubkey *destination_account, uint64_t lamports,
                                       SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    SolAccountMeta account_metas[] =
          ///   0. `[writable, signer]` The source account.
        { { source_account, true, true },
          ///   1. `[writable]` The destination account.
          { destination_account, true, false } };

    util_SystemTransferData data = { 2, lamports };

    SolInstruction instruction;

    instruction.program_id = &(Constants.system_program_id);
    instruction.accounts = account_metas;
    instruction.account_len = sizeof(account_metas) / sizeof(account_metas[0]);
    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    return sol_invoke(&instruction, transaction_accounts, transaction_accounts_len);
}


#endif // UTIL_TRANSFER_LAMPORTS_C
