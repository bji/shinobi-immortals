#pragma once


// Thaws the token account that holds an entry's token, when that token account was left frozen by a Destake that
// skipped the thaw (see the no_thaw flag in user_destake.c).
//
// The token account of an entry is frozen by Stake, so that the token cannot be transferred or burned while this
// program holds a stake account for it, and is normally thawed by Destake when the stake account is returned.  Destake
// can be told not to thaw it, so that a failure of the SPL Token program's thaw can never prevent a stake account from
// being returned to its owner.  This instruction is how the token owner thaws the token account afterwards.
//
// It only thaws the token account of an entry that is not staked (EntryState_Owned), so that it can never be used to
// unfreeze the token of a staked entry, which must stay frozen for as long as the entry is staked.  Stake fails on a
// frozen token account, so this must be done before the entry can be staked again.
static uint64_t user_thaw(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,   block_account,                    ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(1,   entry_account,                    ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,   token_owner_account,              ReadOnly,   Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(3,   token_account,                    ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(4,   entry_mint_account,               ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(5,   authority_account,                ReadOnly,   NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(6,   spl_token_program_account,        ReadOnly,   NotSigner,  KnownAccount_SPLTokenProgram);
    }
    DECLARE_ACCOUNTS_NUMBER(7);

    // This instruction takes no data beyond the instruction code byte, so ensure that no data was supplied
    if (params->data_len != 1) {
        return Error_InvalidDataSize;
    }

    // Get validated block and entry, which checks all validity of those accounts
    const Block *block = get_validated_block(block_account);
    if (!block) {
        return Error_InvalidAccount_First;
    }

    // Ensure that the block is complete; an entry of a block that is not complete can't have been staked
    if (!is_block_complete(block)) {
        return Error_BlockNotComplete;
    }

    // This is the entry data
    const Entry *entry = get_validated_entry_of_block(entry_account, block_account->key);
    if (!entry) {
        return Error_InvalidAccount_First + 1;
    }

    // Get the clock sysvar, needed to determine the entry's state
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }

    // The entry must be owned and not staked.  The token account of a staked entry must stay frozen for as long as
    // the entry is staked, so that the token cannot be transferred or burned with a stake account attached; so a
    // staked entry is refused with its own error.  An entry in any other state is not owned, or (if reauthorized) is
    // retired from this program, which is then no longer the freeze authority of the entry's mint.
    switch (get_entry_state(block, entry, &clock)) {
    case EntryState_Owned:
        break;
    case EntryState_OwnedAndStaked:
        return Error_EntryStaked;
    default:
        return Error_NotOwned;
    }

    // Check to make sure that the entry token is owned by the token owner account, who has signed.  The token account
    // is allowed to be frozen, since thawing it is the purpose of this instruction.
    if (!is_token_owner(token_account, token_owner_account->key, &(entry->mint_pubkey), 1, true)) {
        return Error_InvalidAccount_First + 3;
    }

    // Check to make sure that the correct entry mint account was passed in; it is needed to thaw the token account
    if (!SolPubkey_same(entry_mint_account->key, &(entry->mint_pubkey))) {
        return Error_InvalidAccount_First + 4;
    }

    // Fail cleanly if the token account is not frozen, rather than leaving the SPL Token program to fail the thaw
    if (!is_token_account_frozen(token_account)) {
        return Error_TokenAccountNotFrozen;
    }

    // Thaw the token account, signed by the program authority, which is the freeze authority of the entry's mint.  The
    // token can be transferred again, and the entry can be staked again.
    thaw_token_account(token_account->key, entry_mint_account->key, params->ka, params->ka_num);

    return 0;
}
