//! Checked loading and verification for programs built with Pinocchio (`AccountView`).
//!
//! These are the functions of [`crate::load`], applied to Pinocchio's `AccountView` (from the `solana-account-view`
//! crate, which Pinocchio re-exports as `pinocchio::AccountView`); see there for what each checks.  Each returns a
//! borrow of the account's data as the account type, which must be dropped before the account's data can be borrowed
//! mutably.

use crate::load::{self, AccountType, Error};
use crate::state::{Block, Entry};
use solana_account_view::{AccountView, Ref};
use solana_address::Address;

/// Loads a Shinobi Immortals account as a `T`, checking that the account is owned by the program and that its data
/// is a `T` ([`load::load`])
pub fn load<T: AccountType>(account: &AccountView) -> Result<Ref<'_, T>, Error> {
    let owner = account.owner();
    let data = account.try_borrow().map_err(|_| Error::AccountBorrowFailed)?;
    Ref::try_map(data, |data| load::load::<T>(&owner, data)).map_err(|(_, error)| error)
}


/// Verifies that `entry` is the account of a genuine Shinobi Immortal, which has not been reauthorized to another
/// program, and that `token`, an SPL Token account owned by `wallet`, holds its token; and returns the entry
/// ([`load::verify_immortal`]).  This does not check that `wallet` signed.
pub fn verify_immortal<'a>(entry: &'a AccountView, token: &AccountView, wallet: &Address)
                           -> Result<Ref<'a, Entry>, Error> {
    let entry = load::<Entry>(entry)?;
    let token_data = token.try_borrow().map_err(|_| Error::AccountBorrowFailed)?;
    load::check_immortal(&entry, &token.owner(), &token_data, wallet)?;
    drop(token_data);
    Ok(entry)
}


/// Verifies that `block` is the block of `entry` (which must itself be trusted), and returns the block
/// ([`load::check_block_of_entry`])
pub fn check_block_of_entry<'a>(entry: &Entry, block: &'a AccountView) -> Result<Ref<'a, Block>, Error> {
    if *block.address() != entry.block_pubkey {
        return Err(Error::WrongBlock);
    }
    load::<Block>(block)
}
