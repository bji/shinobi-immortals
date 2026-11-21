//! Shinobi Immortals: the accounts, verification, and instructions of the Shinobi Immortals Solana program.
//!
//! Shinobi Immortals are unique digital assets (each the single token of its own SPL Token mint) that are managed by
//! the Shinobi Immortals program.  The program records everything about each Immortal in an [`Entry`] account: its
//! level, its form, skill, and Ki factor at each level, random numbers for other programs to use, whether it is
//! staked, and how long it has been staked.  This crate lets other programs, and in particular on-chain games that use
//! Immortals as game pieces, read and trust that information.
//!
//! # What is here
//!
//! - [`state`]: every account type of the program ([`Entry`], [`Block`], [`ProgramState`], [`Authority`], [`Bid`],
//!   and [`Whitelist`]), as `#[repr(C)]` structures that mirror the program's own C structures (in `program/inc`)
//!   exactly, read in place from account data without copying.  Every field is public.
//! - [`load`]: checked loading of those accounts from raw account parts, and [`load::verify_immortal`], which checks
//!   that an entry is genuine and that a given wallet owns its token.  These work with any framework.
//! - [`account_info`] (feature `solana`, on by default) and [`account_view`] (feature `pinocchio`): the same checked
//!   loading for the standard Solana crates' `AccountInfo` and for Pinocchio's `AccountView`.
//! - [`id`]: the program's address and the fixed addresses that it uses.
//! - [`pda`]: derivation of the addresses of the program's accounts.
//! - [`calc`]: the program's calculations (prices, bids, Ki harvests), giving exactly the program's results.
//! - [`instruction`] (feature `solana`): a builder for each of the program's instructions, for clients that build
//!   transactions.
//!
//! # Using Immortals in a game
//!
//! A program that is given an Immortal must not trust an `Entry` account just because it decodes: anyone can create
//! an account holding the same bytes.  It must check that the account belongs to the Shinobi Immortals program, and
//! that the player owns the Immortal's token.  [`account_info::verify_immortal`] does all of that:
//!
//! ```ignore
//! use shinobi_immortals::account_info::verify_immortal;
//!
//! // accounts: the player (a signer), the Immortal's entry account, and the token account that holds it
//! let entry = verify_immortal(entry_account, token_account, player.key)?;
//! if !player.is_signer {
//!     return Err(ProgramError::MissingRequiredSignature);
//! }
//!
//! // Only a revealed entry's metadata is final
//! let level = entry.current_level_metadata().ok_or(ProgramError::InvalidAccountData)?;
//! let attack = level.attack();
//! let defense = level.defense();
//! let random = entry.metadata.random[0];
//! ```
//!
//! Things that a game should know about the fields of an entry:
//!
//! - **Reveal.**  An entry's metadata (its form, skill, and Ki factor at every level, and its random numbers) is final
//!   only once the entry is revealed.  Before then, it may be incomplete, and the admin may still change it.  Use
//!   [`Entry::revealed_metadata`] or [`Entry::current_level_metadata`], which give the metadata only for revealed
//!   entries.
//! - **Reauthorization.**  An entry whose `reauthorized` flag is set has been moved to another program, and its
//!   entry account here is no longer maintained.  [`load::verify_immortal`] rejects such entries.
//! - **Staking.**  `owned.stake_account` is the entry's stake account, or all zeroes if it is not staked
//!   ([`Entry::is_staked`]).  A staked Immortal's token account is frozen, so it cannot be transferred.
//! - **Epochs staked.**  `epochs_staked` is brought up to date only by some instructions, so while the entry is staked
//!   it may lag behind.  A game that needs the exact value can have the client put the program's UpdateEpochsStaked
//!   instruction ([`instruction::update_epochs_staked`]) before the game's own instruction in the same transaction,
//!   and then check [`Entry::epochs_staked_is_current`].
//! - **The block.**  The entry's block (`block_pubkey`) holds the time at which the entry's life began
//!   (`block_start_timestamp`); [`load::check_block_of_entry`] verifies a block account given with an entry.
//!
//! # Compatibility
//!
//! The account layouts can never change: the program is not upgradeable.  A later version of the program would be a
//! separate program with its own address, to which owners may move their Immortals (with the ReAuthorize
//! instruction); support for it would be added to this crate alongside this one.
//!
//! Addresses are `solana_address::Address`, which is the `Pubkey` of the standard Solana crates (solana-pubkey 4) and
//! the address type of Pinocchio, so they can be used with either directly.

#![cfg_attr(not(test), no_std)]
#![deny(missing_docs)]

extern crate alloc;

pub mod calc;
pub mod id;
pub mod load;
pub mod pda;
pub mod state;

#[cfg(feature = "solana")]
pub mod account_info;

#[cfg(feature = "solana")]
pub mod instruction;

#[cfg(feature = "pinocchio")]
pub mod account_view;

pub use load::Error;
pub use solana_address::Address;
pub use state::{Authority, Bid, Block, BlockConfiguration, DataType, Entry, EntryMetadata, EntryState, LevelMetadata,
                ProgramState, Whitelist};
