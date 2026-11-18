#pragma once

#include "util/util_commission.c"
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
    // Account program accounts are neither checked nor used, and may be any accounts.  Commission is still charged.
    // Skipping the harvest costs only the token owner, and only their own unharvested Ki.
    uint8_t no_ki_harvest;

} DestakeData;


static uint64_t user_destake(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts.  The accounts that are only used
    // to harvest Ki (6, 7, 9 and 16) are declared here with no requirements, because they must not be able to cause a
    // failure when no Ki is to be harvested; their requirements are checked below if Ki is to be harvested.
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,   funding_account,                  ReadWrite,  Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(1,   block_account,                    ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,   entry_account,                    ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(3,   token_owner_account,              ReadOnly,   Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(4,   token_account,                    ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(5,   stake_account,                    ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(6,   ki_destination_account,           ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(7,   ki_destination_owner_account,     ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(8,   master_stake_account,             ReadWrite,  NotSigner,  KnownAccount_MasterStake);
        DECLARE_ACCOUNT(9,   ki_mint_account,                  ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(10,  authority_account,                ReadOnly,   NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(11,  clock_sysvar_account,             ReadOnly,   NotSigner,  KnownAccount_ClockSysvar);
        DECLARE_ACCOUNT(12,  system_program_account,           ReadOnly,   NotSigner,  KnownAccount_SystemProgram);
        DECLARE_ACCOUNT(13,  stake_program_account,            ReadOnly,   NotSigner,  KnownAccount_StakeProgram);
        DECLARE_ACCOUNT(14,  stake_history_sysvar_account,     ReadOnly,   NotSigner,  KnownAccount_StakeHistorySysvar);
        DECLARE_ACCOUNT(15,  spl_token_program_account,        ReadOnly,   NotSigner,  KnownAccount_SPLTokenProgram);
        DECLARE_ACCOUNT(16,  spl_ata_program_account,          ReadOnly,   NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(17,  entry_mint_account,               ReadOnly,   NotSigner,  KnownAccount_NotKnown);
    }
    DECLARE_ACCOUNTS_NUMBER(18);

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

    // If Ki is to be harvested, then the accounts that are used to harvest it must be the correct accounts with the
    // correct permissions.  These are the checks that the account declarations above would otherwise have made, and
    // they fail with the same errors.
    if (!data->no_ki_harvest) {
        // The Ki destination account must be writable, since Ki is minted into it
        if (!ki_destination_account->is_writable) {
            return Error_InvalidAccountPermissions_First + 6;
        }
        // The Ki mint must be the Ki mint, and writable, since Ki is minted from it
        if (!check_known_account(ki_mint_account, KnownAccount_KiMint)) {
            return Error_InvalidAccount_First + 9;
        }
        if (!ki_mint_account->is_writable) {
            return Error_InvalidAccountPermissions_First + 9;
        }
        // The Associated Token Account program is used to create the Ki destination account if it doesn't exist
        if (!check_known_account(spl_ata_program_account, KnownAccount_SPLATAProgram)) {
            return Error_InvalidAccount_First + 16;
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

    // Check to make sure that the correct entry mint account was passed in; it is needed to thaw the token account
    if (!SolPubkey_same(entry_mint_account->key, &(entry->mint_pubkey))) {
        return Error_InvalidAccount_First + 17;
    }

    // Check to make sure that the stake account passed in is actually staked in the entry
    if (!SolPubkey_same(&(entry->owned.stake_account), stake_account->key)) {
        return Error_InvalidAccount_First + 5;
    }

    // Decode the stake account
    Stake stake;
    if (!decode_stake_account(stake_account, &stake)) {
        return Error_InvalidAccount_First + 5;
    }

    // Harvest Ki, unless the instruction data says not to.  Must be done before commission is charged since
    // commission charge actually reduces the number of lamports in the stake account, which would affect Ki harvest
    // calculations.
    if (!data->no_ki_harvest) {
        harvest_ki(&stake, entry, ki_destination_account, ki_destination_owner_account->key,
                   funding_account->key, clock.epoch, params->ka, params->ka_num);
    }

    // Charge commission, unless no commission charge has been possible for so long that the user must be allowed to
    // leave without one (see DESTAKE_COMMISSION_WAIVER_EPOCHS in constants.h)
    if (!is_destake_commission_waived(entry, clock.epoch)) {
        charge_commission(&stake, block, entry, stake_account->key, clock.epoch, params->ka, params->ka_num);
    }

    // Use stake account program to set all authorities to the token owner; must do this signed since the
    // program authority is currently the withdraw authority of the stake account
    set_stake_authorities_signed(stake_account->key, token_owner_account->key, params->ka, params->ka_num);

    // The stake account has been returned, so thaw the token account that was frozen when the entry was staked; the
    // token can be transferred again.  The token account of a staked entry is always frozen, but this is only done if
    // it actually is, so that if it were somehow not frozen, the stake account could still be returned (the SPL Token
    // program fails an attempt to thaw an account that is not frozen).
    if (is_token_account_frozen(token_account)) {
        thaw_token_account(token_account->key, entry_mint_account->key, params->ka, params->ka_num);
    }

    // No longer staked, all fields in owned should be zeroed out
    sol_memset(&(entry->owned), 0, sizeof(entry->owned));

    return 0;
}
