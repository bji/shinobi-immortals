//! Checked loading and verification for programs built with the standard Solana crates (`AccountInfo`).
//!
//! These are the functions of [`crate::load`], applied to `AccountInfo`; see there for what each checks.  Each
//! returns a borrow of the account's data as the account type, which must be dropped before the account's data can be
//! borrowed mutably.

use crate::load::{self, AccountType, Error};
use crate::state::{Block, Entry};
use core::cell::Ref;
use solana_account_info::AccountInfo;
use solana_address::Address;

/// Borrows the account's data, checks it with `check`, and returns the borrow as a `T`
fn borrow_as<'a, T: AccountType>(account: &'a AccountInfo,
                                 check: impl FnOnce(&[u8]) -> Result<(), Error>) -> Result<Ref<'a, T>, Error> {
    let data = account.try_borrow_data().map_err(|_| Error::AccountBorrowFailed)?;
    check(&data)?;
    // check has established that the data is a T, so this cannot fail
    Ref::filter_map(data, |data| load::from_bytes::<T>(data).ok()).map_err(|_| Error::Misaligned)
}


/// Loads a Shinobi Immortals account as a `T`, checking that the account is owned by the program and that its data
/// is a `T` ([`load::load`])
pub fn load<'a, T: AccountType>(account: &'a AccountInfo) -> Result<Ref<'a, T>, Error> {
    let owner = account.owner;
    borrow_as(account, |data| load::load::<T>(owner, data).map(|_| ()))
}


/// Verifies that `entry` is the account of a genuine Shinobi Immortal, which has not been reauthorized to another
/// program, and that `token`, an SPL Token account owned by `wallet`, holds its token; and returns the entry
/// ([`load::verify_immortal`]).  This does not check that `wallet` signed.
pub fn verify_immortal<'a>(entry: &'a AccountInfo, token: &AccountInfo, wallet: &Address)
                           -> Result<Ref<'a, Entry>, Error> {
    let entry = load::<Entry>(entry)?;
    let token_data = token.try_borrow_data().map_err(|_| Error::AccountBorrowFailed)?;
    load::check_immortal(&entry, token.owner, &token_data, wallet)?;
    Ok(entry)
}


/// Verifies that `block` is the block of `entry` (which must itself be trusted), and returns the block
/// ([`load::check_block_of_entry`])
pub fn check_block_of_entry<'a>(entry: &Entry, block: &'a AccountInfo) -> Result<Ref<'a, Block>, Error> {
    if *block.key != entry.block_pubkey {
        return Err(Error::WrongBlock);
    }
    load::<Block>(block)
}
