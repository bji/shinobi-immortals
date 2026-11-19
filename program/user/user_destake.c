#pragma once

#include "util/util_ki.c"
#include "util/util_stake.c"


typedef struct
{
    // This is the instruction code for Destake
    uint8_t instruction_code;

    // If this is 0, then the Ki that the entry has earned is harvested before the stake account is returned, as the
    // Harvest instruction would do.  If this is 1, then no Ki is harvested; whatever Ki the entry had earned and not
    // yet harvested is forfeited.  No other value is allowed.
    //
    // The purpose of this flag is to make sure that a stake account can always be recovered by its owner.  Harvesting
    // Ki requires the Associated Token Account and SPL Token programs to create the Ki destination account and mint
    // Ki to it, and if either were ever to fail, a Destake that harvests would fail with it.  With this flag set,
    // Destake does nothing with Ki at all: the Ki destination, Ki destination owner, Ki mint and Associated Token
    // Account program accounts are neither checked nor used, and may be any accounts.  Skipping the harvest costs only
    // the token owner, and only their own unharvested Ki.
    uint8_t no_ki_harvest;

    // If this is 0, then the token account that holds the entry's token, which was frozen by Stake, is thawed once the
    // stake account has been returned.  If this is 1, then the token account is not thawed, and is left frozen; the
    // token owner can thaw it afterwards with the Thaw instruction (see user_thaw.c), and must do so before the entry
    // can be staked again.  No other value is allowed.
    //
    // The purpose of this flag is, like no_ki_harvest, to make sure that a stake account can always be recovered by its
    // owner.  Thawing requires the SPL Token program, and if its thaw were ever to fail, a Destake that thaws would
    // fail with it.  With this flag set, the token account is not modified at all: it need not be writable, and the
    // entry mint account is neither checked nor used, and may be any account.  The stake account is still returned
    // and the entry is still no longer staked.  Leaving the token account frozen costs only the token owner, who
    // cannot transfer or burn the token until it is thawed.
    uint8_t no_thaw;

} DestakeData;


static uint64_t user_destake(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts.  The accounts that are only used
    // to harvest Ki (6, 7, 8 and 14) are declared here with no requirements, because they must not be able to cause a
    // failure when no Ki is to be harvested; their requirements are checked below if Ki is to be harvested.  In the
    // same way, the token account (4) is declared read-only and the entry mint account (15) with no requirements,
    // because they only need more than that if the token account is to be thawed; that is checked below.
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,   funding_account,                  ReadWrite,  Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(1,   block_account,                    ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,   entry_account,                    ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(3,   token_owner_account,              ReadOnly,   Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(4,   token_account,                    ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(5,   stake_account,                    ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(6,   ki_destination_account,           ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(7,   ki_destination_owner_account,     ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(8,   ki_mint_account,                  ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(9,   authority_account,                ReadOnly,   NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(10,  clock_sysvar_account,             ReadOnly,   NotSigner,  KnownAccount_ClockSysvar);
        DECLARE_ACCOUNT(11,  system_program_account,           ReadOnly,   NotSigner,  KnownAccount_SystemProgram);
        DECLARE_ACCOUNT(12,  stake_program_account,            ReadOnly,   NotSigner,  KnownAccount_StakeProgram);
        DECLARE_ACCOUNT(13,  spl_token_program_account,        ReadOnly,   NotSigner,  KnownAccount_SPLTokenProgram);
        DECLARE_ACCOUNT(14,  spl_ata_program_account,          ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(15,  entry_mint_account,               ReadOnly,   NotSigner,  KnownAccount_NotKnown);
    }
    DECLARE_ACCOUNTS_NUMBER(16);

    // Make sure that the input data is the correct size
    if (params->data_len != sizeof(DestakeData)) {
        return Error_InvalidDataSize;
    }

    // Cast to instruction data
    const DestakeData *data = (DestakeData *) params->data;

    // The no_ki_harvest flag must be exactly 0 or 1, so that a malformed transaction cannot be mistaken for a request
    // to forfeit Ki
    if (data->no_ki_harvest > 1) {
        return Error_InvalidData_First;
    }

    // The no_thaw flag must be exactly 0 or 1, so that a malformed transaction cannot be mistaken for a request to
    // leave the token account frozen
    if (data->no_thaw > 1) {
        return Error_InvalidData_First + 1;
    }

    // To make the remaining code a little more readable ...
    bool should_harvest_ki = !data->no_ki_harvest;
    bool should_thaw_token = !data->no_thaw;

    // If Ki is to be harvested, then the accounts that are used to harvest it must be the correct accounts with the
    // correct permissions.  These are the checks that the account declarations above would otherwise have made, and
    // they fail with the same errors.
    if (should_harvest_ki) {
        // The Ki destination account must be writable, since Ki is minted into it
        if (!ki_destination_account->is_writable) {
            return Error_InvalidAccountPermissions_First + 6;
        }
        // The Ki mint must be the Ki mint, and writable, since Ki is minted from it
        if (!check_known_account(ki_mint_account, KnownAccount_KiMint)) {
            return Error_InvalidAccount_First + 8;
        }
        if (!ki_mint_account->is_writable) {
            return Error_InvalidAccountPermissions_First + 8;
        }
        // The Associated Token Account program is used to create the Ki destination account if it doesn't exist
        if (!check_known_account(spl_ata_program_account, KnownAccount_SPLATAProgram)) {
            return Error_InvalidAccount_First + 14;
        }
    }

    // Get validated block and entry, which checks all validity of those accounts
    const Block *block = get_validated_block(block_account);
    if (!block) {
        return Error_InvalidAccount_First + 1;
    }

    // Ensure that the block is complete; cannot stake in a block that is not complete yet
    if (!is_block_complete(block)) {
        return Error_BlockNotComplete;
    }

    // This is the entry data
    Entry *entry = get_validated_entry_of_block(entry_account, block_account->key);
    if (!entry) {
        return Error_InvalidAccount_First + 2;
    }

    // Get the clock sysvar, needed below
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }

    // Check to make sure that the entry is staked
    if (get_entry_state(block, entry, &clock) != EntryState_OwnedAndStaked) {
        return Error_NotStaked;
    }

    // Check to make sure that the entry token is owned by the token owner account.  The entry is staked, so its token
    // account is frozen.
    if (!is_token_owner(token_account, token_owner_account->key, &(entry->mint_pubkey), 1, true)) {
        return Error_InvalidAccount_First + 4;
    }

    // If the token account is to be thawed, then it must be writable, and the correct entry mint account must have
    // been passed in, since it is needed to thaw the token account.  These are the checks that the account
    // declarations above would otherwise have made, and they fail with the same errors.
    if (should_thaw_token) {
        if (!token_account->is_writable) {
            return Error_InvalidAccountPermissions_First + 4;
        }
        if (!SolPubkey_same(entry_mint_account->key, &(entry->mint_pubkey))) {
            return Error_InvalidAccount_First + 15;
        }
    }

    // Check to make sure that the stake account passed in is actually staked in the entry
    if (!SolPubkey_same(&(entry->owned.stake_account), stake_account->key)) {
        return Error_InvalidAccount_First + 5;
    }

    // Harvest Ki, unless the instruction data says not to.  The stake account is decoded only if Ki is to be
    // harvested.  This allows a destake with no Ki harvest even if decoding the stake account would fail (which could
    // occur if there were a change to the format of stake accounts).
    if (should_harvest_ki) {
        Stake stake;
        if (!decode_stake_account(stake_account, &stake)) {
            return Error_InvalidAccount_First + 5;
        }

        harvest_ki(&stake, entry, ki_destination_account, ki_destination_owner_account->key, funding_account->key,
                   clock.epoch, params->ka, params->ka_num);
    }

    // Use stake account program to set all authorities to the token owner; must do this signed since the
    // program authority is currently the withdraw authority of the stake account
    set_stake_authorities_signed(stake_account->key, token_owner_account->key, params->ka, params->ka_num);

    // The stake account has been returned, so thaw the token account that was frozen when the entry was staked; the
    // token can be transferred again.  This is skipped if the instruction data says not to, leaving the token account
    // frozen until the token owner runs Thaw.  The token account of a staked entry is always frozen, but this is only
    // done if it actually is, so that if it were somehow not frozen, the stake account could still be returned (the
    // SPL Token program fails an attempt to thaw an account that is not frozen).
    if (should_thaw_token && is_token_account_frozen(token_account)) {
        thaw_token_account(token_account->key, entry_mint_account->key, params->ka, params->ka_num);
    }

    // No longer staked, all fields in owned should be zeroed out
    sol_memset(&(entry->owned), 0, sizeof(entry->owned));

    return 0;
}
