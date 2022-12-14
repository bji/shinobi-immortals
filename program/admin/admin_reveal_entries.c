#pragma once

#include "util/util_token.c"
#include "util/util_transfer_lamports.c"


typedef struct
{
    // This is the instruction code for RevealEntriesData
    uint8_t instruction_code;

    // Index within the block account of the first entry included here
    uint16_t first_entry;

    // These are the salt values that were used to compute the SHA-256 hash of each entry
    salt_t entry_salt[0];

} RevealEntriesData;


// Forward declaration
static uint64_t reveal_single_entry(const Block *block,
                                    Entry *entry,
                                    const Clock *clock,
                                    salt_t salt,
                                    const SolAccountInfo *admin_account,
                                    const SolAccountInfo *authority_account,
                                    const SolAccountInfo *metaplex_metadata_account,
                                    const SolAccountInfo *transaction_accounts,
                                    int transaction_accounts_len,
                                    /* modifies */ uint64_t *total_lamports_to_move);


static uint64_t compute_reveal_entries_data_size(uint16_t entry_count)
{
    const RevealEntriesData *d = 0;

    // The total space needed is from the beginning of RevealEntriesData to the entries element one beyond the
    // total supported (i.e. if there are 100 entries, then then entry at index 100 starts at the first byte beyond
    // the array)
    return ((uint64_t) &(d->entry_salt[entry_count]));
}


static uint64_t admin_reveal_entries(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,   config_account,                ReadOnly,   NotSigner,  KnownAccount_ProgramConfig);
        DECLARE_ACCOUNT(1,   admin_account,                 ReadWrite,  Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,   block_account,                 ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(3,   authority_account,             ReadWrite,  NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(4,   system_program_account,        ReadOnly,   NotSigner,  KnownAccount_SystemProgram);
        DECLARE_ACCOUNT(5,   metaplex_program_account,      ReadOnly,   NotSigner,  KnownAccount_MetaplexProgram);
    }

    // There are 2 accounts per entry, following the 6 fixed accounts
    uint8_t entry_count = (params->ka_num - 6) / 2;

    // Must be exactly the fixed accounts + 2 accounts per entry
    DECLARE_ACCOUNTS_NUMBER(6 + (entry_count * 2));

    // Ensure the the transaction has been authenticated by the admin
    if (!is_admin_account(config_account, admin_account->key)) {
        return Error_PermissionDenied;
    }

    // Make sure that the data is properly sized given the number of entries
    if (params->data_len != compute_reveal_entries_data_size(entry_count)) {
        return Error_InvalidDataSize;
    }

    // Data can be used now
    const RevealEntriesData *data = (RevealEntriesData *) params->data;

    // Get the valid block data
    Block *block = get_validated_block(block_account);
    if (!block) {
        return Error_InvalidAccount_First + 2;
    }

    // Load the clock, which is needed below
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }

    // Ensure that the block is complete; cannot reveal entries of a block that is not complete yet
    if (!is_block_complete(block)) {
        return Error_BlockNotComplete;
    }

    // Ensure that the block has reached its reveal criteria; cannot reveal entries of a block that has not reached
    // reveal
    if (!is_complete_block_revealable(block, &clock)) {
        return Error_BlockNotRevealable;
    }

    // Make sure that the last entry to reveal does not exceed the number of entries in the block
    if ((data->first_entry + entry_count) > block->config.total_entry_count) {
        return Error_InvalidData_First + 1;
    }

    // Keep track of total number of escrow lamports, paid to the authority account by purchasers of "mystery"
    // un-revealed entries, that need to be moved to the admin account now that the entries are revealed.
    uint64_t total_lamports_to_move = 0;

    // Reveal entries one by one
    for (uint16_t i = 0; i < entry_count; i++) {
        uint16_t destination_index = data->first_entry + i;

        // _account_num is defined by DECLARE_ACCOUNTS

        // This is the account info of the entry, as passed into the accounts list
        const SolAccountInfo *entry_account = &(params->ka[_account_num++]);

        // Ensure that the entry account is writable
        if (!entry_account->is_writable) {
            return Error_InvalidAccountPermissions_First + 6;
        }

        // The salt is the corresponding entry in the input data
        salt_t salt = data->entry_salt[i];

        // Get the validated Entry and ensure that it's for the provided block
        Entry *entry = get_validated_entry_of_block(entry_account, block_account->key);
        if (!entry) {
            return Error_InvalidAccount_First + 6;
        }

        // Make sure that it's the correct entry for the index
        if (entry->entry_index != destination_index) {
            return Error_InvalidAccount_First + 6;
        }

        // This is the account info of the metaplex metadata for the entry, as passed into the accounts list
        const SolAccountInfo *metaplex_metadata_account = &(params->ka[_account_num++]);

        // Ensure that it's the correct metadata account for this entry
        if (!SolPubkey_same(metaplex_metadata_account->key, &(entry->metaplex_metadata_pubkey))) {
            return Error_InvalidAccount_First + 7;
        }

        // Ensure that the metaplex metadata account is writable
        if (!metaplex_metadata_account->is_writable) {
            return Error_InvalidAccountPermissions_First + 7;
        }

        // Do the reveal of this entry
        uint64_t result = reveal_single_entry(block, entry, &clock, salt, admin_account, authority_account,
                                              metaplex_metadata_account, params->ka, params->ka_num,
                                              /* modifies */ &total_lamports_to_move);

        // If that reveal failed, then the entire transaction fails
        if (result) {
            return result;
        }
    }

    // All entries revealed successfully.  Move the escrow lamports that needed to move.  This must be done at the end
    // to avoid errors with modified accounts used in cross-program invoke elsewhere in the transaction execution.
    if (total_lamports_to_move) {
        *(admin_account->lamports) += total_lamports_to_move;
        *(authority_account->lamports) -= total_lamports_to_move;
    }

    return 0;
}


// This function reveals a single entry, which means ensuring that it meets all requirements for being a valid
// reveal, and then updates the entry state to their post-reveal values.  It returns nonzero on error, zero on
// success.
static uint64_t reveal_single_entry(const Block *block,
                                    Entry *entry,
                                    const Clock *clock,
                                    salt_t salt,
                                    const SolAccountInfo *admin_account,
                                    const SolAccountInfo *authority_account,
                                    const SolAccountInfo *metaplex_metadata_account,
                                    const SolAccountInfo *transaction_accounts,
                                    int transaction_accounts_len,
                                    /* modifies */ uint64_t *total_lamports_to_move)
{
    // Ensure that the metaplex metadata account passed in is the actual metaplex metadata account for this token
    if (!SolPubkey_same(metaplex_metadata_account->key, &(entry->metaplex_metadata_pubkey))) {
        return Error_InvalidMetadataAccount;
    }

    // Only do a reveal if the entry is waiting for reveal
    switch (get_entry_state(block, entry, clock)) {
    case EntryState_WaitingForRevealUnowned:
        break;
    case EntryState_WaitingForRevealOwned:
        // The SOL that was originally paid by the purchaser was moved into the authority account as a form of escrow,
        // in case the reveal did not happen, the user could request a refund after the reveal period completed.  If a
        // refund was not completed, then move the funds from the escrow to the admin account.  The lamports to move
        // is added to the total value that is accumulated during the tx, to be moved at the very end.
        if (!entry->refund_awarded) {
            *total_lamports_to_move += entry->purchase_price_lamports;
        }
        break;
    default:
        // Already revealed
        return Error_AlreadyRevealed;
    }

    // OK at this point, it is known that the mint account is valid, the token account is valid, the metaplex metadata
    // accounts is valid, and the entry is in a valid state.  So now compute the SHA-256 hash that will ensure that
    // these are correct for the reveal of this entry.
    sha256_t computed_sha256;

    // The computation of the hash is a two step process:
    // 1. Compute the SHA-256 hash of the entry metadata, into a contiguous buffer of bytes.  Then append the 8 bytes
    //    of salt onto the end.
    // 2. Compute SHA-256 of this buffer
    {
        uint8_t buffer[sizeof(sha256_t) + 8];
        {
            SolBytes bytes;
            bytes.addr = (uint8_t *) &(entry->metadata);
            bytes.len = sizeof(entry->metadata);
            if (sol_sha256(&bytes, 1, buffer)) {
                return Error_InvalidHash;
            }
        }

        * (salt_t *) &(((sha256_t *) buffer)[1]) = salt;

        {
            SolBytes bytes;
            bytes.addr = buffer;
            bytes.len = sizeof(buffer);
            if (sol_sha256(&bytes, 1, (uint8_t *) &computed_sha256)) {
                return Error_InvalidHash;
            }
        }
    }

    // Now ensure that the entry's reveal_sha256 hash matches the computed hash.  This then verifies that the entry
    // has had its metadata set already to the correct revealed values.
    if (sol_memcmp(&computed_sha256, &(entry->reveal_sha256), sizeof(sha256_t))) {
        return Error_InvalidHash;
    }

    // Update the metaplex metadata for the entry to include the level 0 state.
    uint64_t ret = set_metaplex_metadata_for_level(entry, 0, metaplex_metadata_account, transaction_accounts,
                                                   transaction_accounts_len);
    if (ret) {
        return ret;
    }

    // Now that the entry is revealed, set its reveal timestamp, and zero out its reveal_sha256, which is what puts
    // the Entry into its revealed state.
    entry->reveal_timestamp = clock->unix_timestamp;
    sol_memset(&(entry->reveal_sha256), 0, sizeof(entry->reveal_sha256));

    // That's all that is needed to complete the reveal of an entry
    return 0;
}
