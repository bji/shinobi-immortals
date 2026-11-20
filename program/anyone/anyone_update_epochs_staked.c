#pragma once

#include "util/util_ki.c"
#include "util/util_stake.c"


// Brings an entry's epochs_staked up to date, so that it is exact as of the current epoch: it then counts every
// completed epoch, up to but not including the current one, in which the entry was staked with stake active at Shinobi
// Systems (see count_epochs_staked in util_ki.c).
//
// epochs_staked is otherwise only brought up to date by instructions that examine the entry's stake account for other
// reasons (Harvest, LevelUp, Destake, Delegate and ReAuthorize), so for a staked entry it may be out of date.  This
// instruction exists so that another program, for example a game, can rely on epochs_staked simply by reading the
// entry, without having to read and interpret the entry's stake account itself: a transaction that includes this
// instruction before the game's own instruction gives the game an exact value.  The game's website can add this
// instruction to such transactions on the user's behalf.
//
// It is permissionless because it can do only one thing, which is to count epochs that the entry has actually been
// staked for, exactly as any of the instructions listed above would.  It never counts an epoch twice, so calling it
// any number of times, at any time, has the same effect as calling it once.
//
// If the entry is not staked, then there is nothing to count: epochs_staked was brought up to date when the entry was
// last destaked (or reauthorized), and cannot change until it is staked again.  This instruction then succeeds without
// doing anything, and the stake account passed in is not used, and may be any account.  This allows a game to include
// this instruction in every transaction that needs an exact epochs_staked, without having to know whether the entry is
// staked.

static uint64_t anyone_update_epochs_staked(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,  entry_account,                 ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        // The entry's stake account if the entry is staked; any account otherwise, since it is then not used
        DECLARE_ACCOUNT(1,  stake_account,                 ReadOnly,   NotSigner,  KnownAccount_NotKnown);
    }
    DECLARE_ACCOUNTS_NUMBER(2);

    // This instruction takes no data beyond the instruction code byte, so ensure that no data was supplied
    if (params->data_len != 1) {
        return Error_InvalidDataSize;
    }

    // This is the entry data
    Entry *entry = get_validated_entry(entry_account);
    if (!entry) {
        return Error_InvalidAccount_First + 0;
    }

    // Get the clock sysvar, needed below
    Clock clock;
    if (sol_get_clock_sysvar(&clock)) {
        return Error_FailedToGetClock;
    }

    // If the entry is not staked, then epochs_staked is already exact, so there is nothing to do
    if (get_entry_state(0, entry, &clock) != EntryState_OwnedAndStaked) {
        return 0;
    }

    // Check to make sure that the stake account passed in is actually staked in the entry
    if (!SolPubkey_same(&(entry->owned.stake_account), stake_account->key)) {
        return Error_InvalidAccount_First + 1;
    }

    // Decode the stake account
    Stake stake;
    if (!decode_stake_account(stake_account, &stake)) {
        return Error_InvalidAccount_First + 1;
    }

    // Count the epochs staked since they were last counted
    count_epochs_staked(&stake, entry, clock.epoch);

    return 0;
}
