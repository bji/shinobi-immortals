#pragma once

#include "util/util_ki.c"

typedef struct
{
    // This is the instruction code for Reauthorize
    uint8_t instruction_code;

    // If true, the Metaplex metadata update authority of the entry is set to new_metadata_authority.  If false, the
    // Metaplex Metadata program is not invoked at all, and the only thing done is to return the entry's stake account
    // to the token owner; this requires that the entry be staked, since otherwise there would be nothing to do.
    //
    // This exists so that a failure of the Metaplex Metadata program (which this program does not control) can never
    // prevent a stake account from being returned to its owner.  In that situation the entry's metadata remains
    // under the control of this program, but the stake account, which is by far the more valuable asset, is still
    // recovered.  The metadata can be reauthorized later, by a second ReAuthorize with this value set to true, if
    // the Metaplex Metadata program becomes usable again.
    //
    // Must be 0 (false) or 1 (true).
    uint8_t reauthorize_metadata;

    // If reauthorize_metadata is true, this is the new update authority of the entry's Metaplex metadata, which would
    // normally be the authority of a newer version of this program.  It is ignored if reauthorize_metadata is false.
    //
    // Note that this is NOT given any authority over the entry's stake account.  If the entry is staked, its stake
    // account is always returned to the token owner (who must sign this transaction), exactly as Destake would
    // return it, except that Ki is not harvested.  There is deliberately no way to
    // send the stake account anywhere else: no mistaken address can lose it, and the program that takes over the
    // entry never holds a stake account that it has no record of.  The owner can stake it to the entry again using
    // that program's own stake instruction, in the same transaction if desired; the stake account remains delegated
    // throughout, so no rewards are missed.
    //
    // Because Ki is not harvested here, this instruction succeeds even if Ki cannot be harvested.  A transaction can
    // include a Harvest instruction before this one; the user, who must sign, can decline to sign a transaction that
    // omits it.
    SolPubkey new_metadata_authority;

} ReauthorizeData;


static uint64_t special_reauthorize(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,   state_account,                 ReadOnly,   NotSigner,   KnownAccount_ProgramState);
        DECLARE_ACCOUNT(1,   admin_account,                 ReadOnly,   Signer,      KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,   entry_account,                 ReadWrite,  NotSigner,   KnownAccount_NotKnown);
        DECLARE_ACCOUNT(3,   token_owner_account,           ReadOnly,   Signer,      KnownAccount_NotKnown);
        DECLARE_ACCOUNT(4,   token_account,                 ReadOnly,   NotSigner,   KnownAccount_NotKnown);
        DECLARE_ACCOUNT(5,   entry_metadata_account,        ReadOnly,   NotSigner,   KnownAccount_NotKnown);
        DECLARE_ACCOUNT(6,   stake_account,                 ReadOnly,   NotSigner,   KnownAccount_NotKnown);
        DECLARE_ACCOUNT(7,   authority_account,             ReadOnly,   NotSigner,   KnownAccount_Authority);
        DECLARE_ACCOUNT(8,   clock_sysvar_account,          ReadOnly,   NotSigner,   KnownAccount_ClockSysvar);
        DECLARE_ACCOUNT(9,   metaplex_program_account,      ReadOnly,   NotSigner,   KnownAccount_MetaplexProgram);
        DECLARE_ACCOUNT(10,  stake_program_account,         ReadOnly,   NotSigner,   KnownAccount_StakeProgram);
        DECLARE_ACCOUNT(11,  entry_mint_account,            ReadOnly,   NotSigner,   KnownAccount_NotKnown);
        DECLARE_ACCOUNT(12,  spl_token_program_account,     ReadOnly,   NotSigner,   KnownAccount_SPLTokenProgram);
    }
    DECLARE_ACCOUNTS_NUMBER(13);

    // Ensure that the transaction was authorized by the admin
    if (!is_admin_account(state_account, admin_account->key)) {
        return Error_PermissionDenied;
    }

    // Ensure that the data is of the correct size
    if (params->data_len != sizeof(ReauthorizeData)) {
        return Error_InvalidDataSize;
    }

    // Can safely use the data now
    const ReauthorizeData *data = (ReauthorizeData *) params->data;

    // This is the entry data
    Entry *entry = get_validated_entry(entry_account);
    if (!entry) {
        return Error_InvalidAccount_First + 2;
    }

    // Get the clock sysvar, needed below
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }

    // Make sure that the entry is in an Owned or OwnedAndStaked state
    bool is_staked;
    switch (get_entry_state(0, entry, &clock)) {
    case EntryState_Owned:
        is_staked = false;
        break;
    case EntryState_OwnedAndStaked:
        is_staked = true;
        break;
    default:
        return Error_InvalidAccount_First + 2;
    }

    // Check to make sure that the entry token is owned by the token owner account.  If the entry is staked, then its
    // token account is frozen.
    if (!is_token_owner(token_account, token_owner_account->key, &(entry->mint_pubkey), 1, true)) {
        return Error_InvalidAccount_First + 3;
    }

    // The token account that holds the token of a staked entry is frozen (see freeze_token_account), and must be
    // thawed when the stake account is returned.  This is decided by the actual state of the token account and not by
    // whether the entry is staked, so that a token account can never be left frozen by an entry which this program
    // will no longer operate on.
    bool is_frozen = is_token_account_frozen(token_account);

    // The reauthorize_metadata flag must be exactly 0 or 1, so that a malformed transaction cannot be mistaken for
    // a request to skip (or to perform) the metadata reauthorization
    if (data->reauthorize_metadata > 1) {
        return Error_InvalidData_First;
    }

    // If the metadata is not being reauthorized, then the only thing that this instruction can do is reauthorize the
    // entry's stake account.  If the entry is not staked, there is nothing to do, so fail rather than succeed without
    // having done anything.
    if (!data->reauthorize_metadata && !is_staked) {
        return Error_NotStaked;
    }

    // All checks are made before any cross-program invocation, so that an invalid transaction fails with this
    // program's error rather than part way through.

    // If the metadata is to be reauthorized, check the metadata account.  If it is not, then the metadata account is
    // ignored entirely (any account may be passed in), so that nothing about the state of the metadata account or of
    // the Metaplex Metadata program can cause this instruction to fail.
    if (data->reauthorize_metadata) {
        // Check to make sure that the correct metaplex metadata account was provided
        if (!SolPubkey_same(entry_metadata_account->key, &(entry->metaplex_metadata_pubkey))) {
            return Error_InvalidAccount_First + 5;
        }

        // And ensure that it's read-write; this isn't enforced in the declared accounts list because the metadata
        // account is unused when the metadata is not being reauthorized
        if (!entry_metadata_account->is_writable) {
            return Error_InvalidAccountPermissions_First + 5;
        }
    }

    // The entry mint account is needed to thaw the token account, and to pass on the freeze authority of the mint
    // when the metadata is reauthorized.  It is ignored otherwise.
    if (is_frozen || data->reauthorize_metadata) {
        // Check to make sure that the correct entry mint account was provided
        if (!SolPubkey_same(entry_mint_account->key, &(entry->mint_pubkey))) {
            return Error_InvalidAccount_First + 11;
        }
    }

    // The freeze authority of the mint is only changed when the metadata is reauthorized, so the mint only needs to
    // be read-write in that case
    if (data->reauthorize_metadata && !entry_mint_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 11;
    }

    // The token account is only changed if it is to be thawed, so it only needs to be read-write in that case
    if (is_frozen && !token_account->is_writable) {
        return Error_InvalidAccountPermissions_First + 4;
    }

    // If the entry is staked, check the stake account
    if (is_staked) {
        // Make sure that the passed-in stake account is the correct one
        if (!SolPubkey_same(stake_account->key, &(entry->owned.stake_account))) {
            return Error_InvalidAccount_First + 6;
        }

        // And ensure that it's read-write; couldn't enforce this in the declared accounts list, because the system
        // program (all zeroes) is expected if there is no stake account to modify, and that will be forced to
        // read-only by the runtime regardless of the transaction settings on the account
        if (!stake_account->is_writable) {
            return Error_InvalidAccountPermissions_First + 6;
        }
    }

    // Set the metaplex metadata authority of the entry to the new authority, if requested.  The Metaplex Metadata
    // program is not invoked at all otherwise.
    if (data->reauthorize_metadata) {
        set_metaplex_metadata_authority(&(entry->metaplex_metadata_pubkey), &(data->new_metadata_authority),
                                        params->ka, params->ka_num);
    }

    // Thaw the token account if it is frozen, since the stake account that it was frozen for is about to be returned.
    // This must be done before the freeze authority of the mint is passed on below, because this program cannot thaw
    // anything afterwards.
    if (is_frozen) {
        thaw_token_account(token_account->key, entry_mint_account->key, params->ka, params->ka_num);
    }

    // If the metadata was reauthorized, then the entry is from now on operated by whichever program controls
    // new_metadata_authority, so pass the freeze authority of the entry's mint to that same authority.  That program
    // can then protect the token while it holds a stake account for it, exactly as this program did.  This program
    // has no further use for the freeze authority, since the entry is retired below and can never be staked here
    // again.  A stake-only reauthorize leaves the freeze authority with this program, along with the metadata.
    if (data->reauthorize_metadata) {
        set_mint_freeze_authority(entry_mint_account->key, &(data->new_metadata_authority), params->ka,
                                  params->ka_num);
    }

    // If the entry is staked, then return its stake account to the token owner, by setting both of its authorities
    // to the token owner; must do this signed since the program authority is currently the withdraw authority of the
    // stake account.  It has already been verified above that the token owner signed this transaction and owns the
    // token account that holds the entry's token.
    if (is_staked) {
        // First count the epochs staked since they were last counted, so that the entry's record of them is complete
        // for whichever program operates it next; this is the last chance to count them.  If the stake account cannot
        // be decoded, they are forfeited, so that this instruction can still return it.
        Stake stake;
        if (decode_stake_account(stake_account, &stake)) {
            count_epochs_staked(&stake, entry, clock.epoch);
        }

        set_stake_authorities_signed(&(entry->owned.stake_account), token_owner_account->key,
                                     params->ka, params->ka_num);

        // The entry is no longer staked, so all fields in owned must be zeroed out.  This is required, not just
        // tidiness: the entry is considered staked for as long as owned.stake_account is set, and this program no
        // longer has any authority over the stake account.  If the entry still referred to it, then Harvest would
        // continue to mint Ki for any increase in its delegated stake, which its owner could now produce at will
        // (for example by merging other stake into it); and Destake would fail forever, leaving the entry unable
        // to ever be staked again.
        sol_memset(&(entry->owned), 0, sizeof(entry->owned));
    }

    // If the metadata was reauthorized, the entry now belongs to whichever program controls new_metadata_authority,
    // and is retired from this one: mark it so that get_entry_state reports EntryState_Reauthorized from now on, which
    // every instruction handler rejects.  Without this the entry would remain Owned here, and could be staked again
    // to earn this program's Ki while also being operated by its new program.  A stake-only reauthorize does not
    // retire the entry, since its metadata is still under this program's control.  By this point any stake account
    // has been returned to the token owner (above), so nothing of the user's is held by a retired entry.
    if (data->reauthorize_metadata) {
        entry->reauthorized = true;
    }

    return 0;
}
