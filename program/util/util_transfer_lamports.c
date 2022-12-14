#pragma once

typedef struct __attribute__((packed))
{
    uint32_t instruction_code; // 2 for Transfer

    uint64_t amount;

} util_SystemTransferData;


static uint64_t util_transfer(const SolPubkey *source_account, const SolPubkey *destination_account,
                              uint64_t lamports, const SolSignerSeed *seeds, int seeds_count,
                              const SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    SolInstruction instruction;

    instruction.program_id = &(Constants.system_program_pubkey);

    SolAccountMeta account_metas[] =
          ///   0. `[writable, signer]` The source account.
        { { (SolPubkey *) source_account, true, true },
          ///   1. `[writable]` The destination account.
          { (SolPubkey *) destination_account, true, false } };

    instruction.accounts = account_metas;
    instruction.account_len = ARRAY_LEN(account_metas);

    util_SystemTransferData data = { 2, lamports };

    instruction.data = (uint8_t *) &data;
    instruction.data_len = sizeof(data);

    SolSignerSeeds signer_seeds = { seeds, seeds_count };

    return seeds ?
        sol_invoke_signed(&instruction, transaction_accounts, transaction_accounts_len, &signer_seeds, 1) :
        sol_invoke(&instruction, transaction_accounts, transaction_accounts_len);
}


static uint64_t util_transfer_lamports(const SolPubkey *source_account, const SolPubkey *destination_account,
                                       uint64_t lamports, const SolAccountInfo *transaction_accounts,
                                       int transaction_accounts_len)
{
    return util_transfer(source_account, destination_account, lamports, 0, 0, transaction_accounts,
                         transaction_accounts_len);
}


static uint64_t util_transfer_lamports_signed(const SolPubkey *source_account, const SolPubkey *destination_account,
                                              uint64_t lamports, const SolSignerSeed *seeds, int seeds_count,
                                              const SolAccountInfo *transaction_accounts, int transaction_accounts_len)
{
    return util_transfer(source_account, destination_account, lamports, seeds, seeds_count, transaction_accounts,
                         transaction_accounts_len);
}
