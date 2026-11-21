//! The account types of the Shinobi Immortals program.
//!
//! Each type is a `#[repr(C)]` structure that mirrors the program's own C structure (in `program/inc`) exactly, with
//! the same field names, so that account data can be read in place (see [`crate::load`]).  The padding that the C
//! compiler inserts between fields is made explicit as private `_padding` fields, which hold whatever the account
//! holds there (not necessarily zeroes).  The layouts are checked against the C compiler's, field by field, by the
//! tests.
//!
//! C `bool` fields are `u8` here, since a Rust `bool` cannot hold every byte value that an account might hold; each
//! has an accessor method of the same name that returns a `bool` (nonzero is true, as in C).  C enums are `u32`.
//!
//! Every field is public.  The program is not upgradeable, so these layouts never change.

use bytemuck::{Pod, Zeroable};
use solana_address::Address;

/// The data type of an account, which is the first field (`data_type`) of each of the program's accounts, so that
/// one kind of account can never be mistaken for another.  `DataType` in `program/inc/data_type.h`.
#[repr(u32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum DataType {
    /// Never used for valid data: it is what an uninitialized account would hold
    Invalid = 0,
    /// [`ProgramState`]
    ProgramState = 1,
    /// [`Block`]
    Block = 2,
    /// [`Entry`]
    Entry = 3,
    /// [`Bid`]
    Bid = 4,
    /// [`Whitelist`]
    Whitelist = 5,
    /// [`Authority`]
    Authority = 6,
}

impl DataType {
    /// Returns the data type of the given code, or `None` if it is not a data type
    pub const fn from_u32(code: u32) -> Option<DataType> {
        match code {
            0 => Some(DataType::Invalid),
            1 => Some(DataType::ProgramState),
            2 => Some(DataType::Block),
            3 => Some(DataType::Entry),
            4 => Some(DataType::Bid),
            5 => Some(DataType::Whitelist),
            6 => Some(DataType::Authority),
            _ => None,
        }
    }
}


/// The states that an entry can be in, as the program decides them (`EntryState` in `program/inc/entry.h`; see
/// [`Entry::state`]).
#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum EntryState {
    /// Not revealed yet; which of the pre-reveal states it is cannot be known without the entry's block
    PreReveal = 0,
    /// Not revealed yet, and not owned
    PreRevealUnowned = 1,
    /// Owned (bought as a mystery), but its block has not met its reveal criteria yet
    PreRevealOwned = 2,
    /// Its block has met its reveal criteria, but the entry has not been revealed yet; not owned
    WaitingForRevealUnowned = 3,
    /// Its block has met its reveal criteria, but the entry has not been revealed yet; owned
    WaitingForRevealOwned = 4,
    /// Revealed, and in its auction
    InAuction = 5,
    /// Its auction is over, and it is waiting to be claimed by the winning bidder
    WaitingToBeClaimed = 6,
    /// Revealed, and not owned (its auction, if it had one, is over without bids)
    Unowned = 7,
    /// Owned and revealed, but not staked
    Owned = 8,
    /// Owned, revealed, and staked
    OwnedAndStaked = 9,
    /// Retired: its metadata has been reauthorized to another program, which manages it from then on
    Reauthorized = 10,
}


/// Returns the bytes of a fixed size C string field, up to (not including) its first NUL byte
fn c_string(field: &[u8]) -> &[u8] {
    match field.iter().position(|&b| b == 0) {
        Some(length) => &field[..length],
        None => field,
    }
}


// Program state ------------------------------------------------------------------------------------------------------

/// The program state account (at [`crate::id::PROGRAM_STATE`]), which identifies the admin.  `ProgramState` in
/// `program/inc/program_state.h`.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Pod, Zeroable)]
pub struct ProgramState {
    /// [`DataType::ProgramState`]
    pub data_type: u32,
    /// The admin, the only account that may perform the admin instructions
    pub admin_pubkey: Address,
}


// Authority ----------------------------------------------------------------------------------------------------------

/// The program's authority account (at [`crate::id::AUTHORITY`]).  `Authority` in `program/inc/authority.h`.
///
/// The authority account holds the purchase prices of mysteries in escrow, from when each is bought until its entry is
/// revealed (the price then goes to the admin) or refunded (the price goes back to the buyer).
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Pod, Zeroable)]
pub struct Authority {
    /// [`DataType::Authority`]
    pub data_type: u32,
    _padding0: [u8; 4],
    /// The total held in escrow: the sum of the purchase prices of all mysteries that have been bought and since been
    /// neither revealed nor refunded
    pub mystery_escrow_lamports: u64,
}


// Block --------------------------------------------------------------------------------------------------------------

/// The configuration of a block, set when it is created and never changed.  `BlockConfiguration` in
/// `program/inc/block.h`, which describes each field fully.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Pod, Zeroable)]
pub struct BlockConfiguration {
    /// The block's group
    pub group_number: u32,
    /// The block's number within its group
    pub block_number: u32,
    /// The number of entries in the block
    pub total_entry_count: u16,
    /// The number of entries that may be bought as mysteries (before reveal)
    pub total_mystery_count: u16,
    /// Seconds from the completion of the block (`block_start_timestamp`) to the end of its mystery phase
    pub mystery_phase_duration: u32,
    /// The price of a mystery at the start of the mystery phase
    pub mystery_start_price_lamports: u64,
    /// Seconds after all mysteries are sold within which entries must be revealed, after which a mystery may be
    /// refunded
    pub reveal_period_duration: u32,
    _padding0: [u8; 4],
    /// The final price of a mystery, the starting price of an auction, and the final price of a revealed entry
    pub minimum_price_lamports: u64,
    /// Whether revealed entries are auctioned (nonzero) or sold at a decaying price (zero); see
    /// [`BlockConfiguration::has_auction()`]
    pub has_auction: u8,
    _padding1: [u8; 3],
    /// Seconds of each entry's auction, or of the decay of its price from `final_start_price_lamports` to
    /// `minimum_price_lamports`
    pub duration: u32,
    /// The price of a revealed entry at the start of its sale, if the block has no auction
    pub final_start_price_lamports: u64,
    /// Seconds after the completion of the block during which only whitelisted accounts may buy, or 0 if the block
    /// has no whitelist period
    pub whitelist_duration: u32,
    /// The expected annual stake reward rate, in basis points (490 is 4.90%), of a stake account delegated to the
    /// Shinobi Systems validator, which calibrates the level credit curve of every entry of the block.  The block's
    /// entries cannot be levelled up if it is 0.
    pub expected_annual_reward_bps: u32,
}

impl BlockConfiguration {
    /// Whether the block's revealed entries are auctioned (rather than sold at a decaying price)
    pub fn has_auction(&self) -> bool {
        self.has_auction != 0
    }
}


/// A block: a group of entries that are created and sold together.  `Block` in `program/inc/block.h`.
///
/// A block account's data is this structure followed by the block's `entries_added_bitmap`: one bit for each entry of
/// the block, set once that entry has been added (see [`Block::entry_added`]).
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Pod, Zeroable)]
pub struct Block {
    /// [`DataType::Block`]
    pub data_type: u32,
    _padding0: [u8; 4],
    /// The block's configuration, which never changes.  Entries copy some of it into their own fields when they are
    /// added (see `program/inc/block.h`).
    pub config: BlockConfiguration,
    /// The number of entries added so far
    pub added_entries_count: u16,
    _padding1: [u8; 6],
    /// When the last entry was added and the block became complete (0 until then).  This is the beginning of the
    /// mystery phase, and the beginning of the life span of each of the block's entries.
    pub block_start_timestamp: i64,
    /// The number of mysteries sold
    pub mysteries_sold_count: u16,
    _padding2: [u8; 6],
    /// When the last mystery was sold, if all were; otherwise 0 (use [`Block::mystery_phase_end_timestamp()`])
    pub mystery_phase_end_timestamp: i64,
}

impl Block {
    /// Whether every entry of the block has been added
    pub fn is_complete(&self) -> bool {
        self.added_entries_count == self.config.total_entry_count
    }

    /// Whether the (complete) block has met its reveal criteria at time `unix_timestamp`: all of its mysteries have
    /// been sold, or its mystery phase is over.  `is_complete_block_revealable` of the program.
    pub fn is_revealable(&self, unix_timestamp: i64) -> bool {
        (self.mysteries_sold_count == self.config.total_mystery_count) ||
            (unix_timestamp > self.block_start_timestamp.saturating_add(self.config.mystery_phase_duration as i64))
    }

    /// When the block's mystery phase ends (or ended): when its last mystery was sold, if all were, or else when its
    /// mystery phase duration elapses.  `get_mystery_phase_end_timestamp` of the program.
    pub fn mystery_phase_end_timestamp(&self) -> i64 {
        if self.mysteries_sold_count == self.config.total_mystery_count {
            self.mystery_phase_end_timestamp
        }
        else {
            self.block_start_timestamp.saturating_add(self.config.mystery_phase_duration as i64)
        }
    }

    /// Whether entry `entry_index` has been added, according to the `entries_added_bitmap` that follows the block in
    /// the account data `data` (all of the block account's data, from its start)
    pub fn entry_added(data: &[u8], entry_index: u16) -> bool {
        let byte = core::mem::size_of::<Block>() + ((entry_index / 8) as usize);
        data.get(byte).is_some_and(|b| (b & (1 << (entry_index % 8))) != 0)
    }
}


// Entry --------------------------------------------------------------------------------------------------------------

/// An entry's metadata at one of its levels.  `LevelMetadata` in `program/inc/entry.h`.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Pod, Zeroable)]
pub struct LevelMetadata {
    /// The form of the entry at this level
    pub form: u32,
    /// Attack and defense skill: the top 4 bits are attack, the bottom 4 are defense (see
    /// [`LevelMetadata::attack`] and [`LevelMetadata::defense`])
    pub skill: u8,
    _padding0: [u8; 3],
    /// The Ki earned per SOL of stake rewards earned by the entry's stake account, at this level
    pub ki_factor: u32,
    /// The name of the entry at this level: a NUL terminated string, unless it fills the field (see
    /// [`LevelMetadata::name()`])
    pub name: [u8; 32],
    /// The title of the entry at this level: a NUL terminated string, unless it fills the field
    pub title: [u8; 32],
    /// The URI of the entry's Metaplex metadata at this level: a NUL terminated string, unless it fills the field
    pub uri: [u8; 200],
    /// The SHA-256 of the contents of `uri`
    pub uri_contents_sha256: [u8; 32],
}

impl LevelMetadata {
    /// The attack skill, from 0 to 15: the top 4 bits of `skill`
    pub fn attack(&self) -> u8 {
        self.skill >> 4
    }

    /// The defense skill, from 0 to 15: the bottom 4 bits of `skill`
    pub fn defense(&self) -> u8 {
        self.skill & 0x0f
    }

    /// The name, as UTF-8 (`None` if it is not valid UTF-8)
    pub fn name(&self) -> Option<&str> {
        core::str::from_utf8(c_string(&self.name)).ok()
    }

    /// The title, as UTF-8 (`None` if it is not valid UTF-8)
    pub fn title(&self) -> Option<&str> {
        core::str::from_utf8(c_string(&self.title)).ok()
    }

    /// The URI, as UTF-8 (`None` if it is not valid UTF-8)
    pub fn uri(&self) -> Option<&str> {
        core::str::from_utf8(c_string(&self.uri)).ok()
    }
}


/// The number of levels of an entry, 0 through 8 ([`Entry::level`] indexes [`EntryMetadata::level_metadata`])
pub const LEVEL_COUNT: usize = 9;

/// The highest level of an entry
pub const MAX_LEVEL: u8 = 8;


/// An entry's metadata.  `EntryMetadata` in `program/inc/entry.h`, which describes each field fully.
///
/// The metadata is final only once the entry is revealed: until then it may be incomplete, and the admin may still
/// change it.  [`Entry::revealed_metadata`] returns it only when it is final.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Pod, Zeroable)]
pub struct EntryMetadata {
    /// The whole level credits needed to reach level 1; each later level needs 50% more than the one before
    pub level_1_credits: u32,
    /// Random numbers, generated for each entry, for other programs to use
    pub random: [u32; 16],
    /// The metadata of each level, indexed by level (0 through 8)
    pub level_metadata: [LevelMetadata; LEVEL_COUNT],
    /// The fraction, of `u32::MAX`, by which the entry's Ki harvests are reduced for each day of its life span (see
    /// [`crate::calc::compute_ki_harvest_retention`])
    pub ki_harvest_reduction_per_day: u32,
}


/// The part of an entry that is used while it is in an auction.  The anonymous `auction` struct of `Entry` in
/// `program/inc/entry.h`.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Pod, Zeroable)]
pub struct EntryAuction {
    /// The highest bid so far, or 0 if there have been none
    pub highest_bid_lamports: u64,
    /// The bid account of the highest bid
    pub winning_bid_pubkey: Address,
}


/// The part of an entry that is used while it is owned.  The anonymous `owned` struct of `Entry` in
/// `program/inc/entry.h`.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Pod, Zeroable)]
pub struct EntryOwned {
    /// The entry's stake account, or all zeroes if it is not staked (see [`Entry::is_staked`])
    pub stake_account: Address,
    /// The stake account's lamports when Ki was last harvested
    pub last_ki_harvest_stake_account_lamports: u64,
}


/// An entry: one Shinobi Immortal.  `Entry` in `program/inc/entry.h`, which describes each field fully.
///
/// The Immortal itself is the single token of the mint `mint_pubkey`; whoever holds that token owns the Immortal (see
/// [`crate::load::verify_immortal`]).
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Pod, Zeroable)]
pub struct Entry {
    /// [`DataType::Entry`]
    pub data_type: u32,
    /// The entry's block
    pub block_pubkey: Address,
    /// The group of the entry's block
    pub group_number: u32,
    /// The number of the entry's block within its group
    pub block_number: u32,
    /// The entry's index within its block
    pub entry_index: u16,
    /// The mint of the entry's token
    pub mint_pubkey: Address,
    /// The entry's own token account, which holds the token from when the entry is created until it is bought or its
    /// auction is settled.  (After that, the token is wherever its owner keeps it.)
    pub token_pubkey: Address,
    /// The entry's Metaplex metadata
    pub metaplex_metadata_pubkey: Address,
    _padding0: [u8; 2],
    /// The entry's minimum price, copied from its block's configuration
    pub minimum_price_lamports: u64,
    /// Whether the entry is auctioned when revealed (see [`Entry::has_auction()`]), copied from its block's
    /// configuration
    pub has_auction: u8,
    _padding1: [u8; 3],
    /// Seconds of the entry's auction (increased by bids that extend it), or of the decay of its price
    pub duration: u32,
    /// The price of the revealed entry at the start of its sale, if it has no auction
    pub non_auction_start_price_lamports: u64,
    /// Before reveal, the SHA-256 of the entry's metadata and salt, which the reveal is checked against; all zeroes
    /// once revealed (see [`Entry::is_revealed`])
    pub reveal_sha256: [u8; 32],
    /// When the entry was revealed
    pub reveal_timestamp: i64,
    /// The price that the entry was bought for, or 0 if it has not been bought
    pub purchase_price_lamports: u64,
    /// Whether the purchase price of the entry, bought as a mystery, was refunded (see [`Entry::refund_awarded()`])
    pub refund_awarded: u8,
    /// Whether the entry has been reauthorized to another program, which manages it from then on; set once and never
    /// cleared (see [`Entry::reauthorized()`])
    pub reauthorized: u8,
    _padding2: [u8; 6],
    /// Used while the entry is in an auction
    pub auction: EntryAuction,
    /// Used while the entry is owned
    pub owned: EntryOwned,
    /// The entry's current level, 0 through 8
    pub level: u8,
    _padding3: [u8; 3],
    /// The entry's metadata, which is final only once the entry is revealed (see [`Entry::revealed_metadata`])
    pub metadata: EntryMetadata,
    /// The level credit window in which `ki_applied_this_window` last changed
    pub most_recent_ki_applied_window: u64,
    /// The Ki applied in that window
    pub ki_applied_this_window: u64,
    /// The level credits earned over the entry's whole life, in tenths of a credit
    pub total_level_credits: u64,
    /// The number of whole epochs for which the entry has been staked, with active stake delegated to Shinobi Systems.
    /// This belongs to the entry, not its owner, and is never reset.  While the entry is staked, it may not yet count
    /// the epochs since `epochs_staked_counted_to_epoch` (see [`Entry::epochs_staked_is_current`]).
    pub epochs_staked: u64,
    /// Every epoch before this one has been counted in `epochs_staked`; only meaningful while the entry is staked
    pub epochs_staked_counted_to_epoch: u64,
}

impl Entry {
    /// Whether the entry is auctioned when revealed
    pub fn has_auction(&self) -> bool {
        self.has_auction != 0
    }

    /// Whether the purchase price of the entry was refunded
    pub fn refund_awarded(&self) -> bool {
        self.refund_awarded != 0
    }

    /// Whether the entry has been reauthorized to another program, and so is retired from this one
    pub fn reauthorized(&self) -> bool {
        self.reauthorized != 0
    }

    /// Whether the entry has been revealed, which makes its metadata final
    pub fn is_revealed(&self) -> bool {
        self.reveal_sha256 == [0; 32]
    }

    /// Whether the entry has been bought
    pub fn is_purchased(&self) -> bool {
        self.purchase_price_lamports != 0
    }

    /// Whether the entry is staked: whether it holds a stake account
    pub fn is_staked(&self) -> bool {
        self.owned.stake_account != Address::new_from_array([0; 32])
    }

    /// The entry's metadata, if the entry has been revealed; `None` before then, when the metadata is not final
    pub fn revealed_metadata(&self) -> Option<&EntryMetadata> {
        if self.is_revealed() { Some(&self.metadata) } else { None }
    }

    /// The metadata of the entry's current level, if the entry has been revealed (and its level is valid)
    pub fn current_level_metadata(&self) -> Option<&LevelMetadata> {
        self.revealed_metadata()?.level_metadata.get(self.level as usize)
    }

    /// Whether `epochs_staked` is exact in epoch `current_epoch`: always if the entry is not staked, and otherwise if
    /// every epoch before the current one has been counted.  If it is not, the program's UpdateEpochsStaked
    /// instruction brings it up to date (see `crate::instruction::update_epochs_staked`).
    pub fn epochs_staked_is_current(&self, current_epoch: u64) -> bool {
        !self.is_staked() || (self.epochs_staked_counted_to_epoch >= current_epoch)
    }

    /// The entry's state at time `unix_timestamp`, exactly as the program decides it (`get_entry_state` of
    /// `program/util/util_entry.c`).  The specific pre-reveal states can only be told apart with the entry's block;
    /// without it, an unrevealed entry is [`EntryState::PreReveal`].
    pub fn state(&self, block: Option<&Block>, unix_timestamp: i64) -> EntryState {
        if self.reauthorized() {
            return EntryState::Reauthorized;
        }

        if self.is_revealed() {
            if self.is_purchased() {
                return if self.is_staked() { EntryState::OwnedAndStaked } else { EntryState::Owned };
            }
            if self.has_auction() {
                if self.reveal_timestamp.saturating_add(self.duration as i64) > unix_timestamp {
                    return EntryState::InAuction;
                }
                if self.auction.highest_bid_lamports != 0 {
                    return EntryState::WaitingToBeClaimed;
                }
            }
            return EntryState::Unowned;
        }

        match block {
            None => EntryState::PreReveal,
            Some(block) => {
                if block.is_revealable(unix_timestamp) {
                    if self.is_purchased() { EntryState::WaitingForRevealOwned }
                    else { EntryState::WaitingForRevealUnowned }
                }
                else if self.is_purchased() {
                    EntryState::PreRevealOwned
                }
                else {
                    EntryState::PreRevealUnowned
                }
            }
        }
    }
}


// Bid ----------------------------------------------------------------------------------------------------------------

/// A bid on an entry in auction, whose lamports are the bid.  `Bid` in `program/inc/bid.h`.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Pod, Zeroable)]
pub struct Bid {
    /// [`DataType::Bid`]
    pub data_type: u32,
    /// The mint of the entry bid on
    pub mint_pubkey: Address,
    /// The bidder
    pub bidder_pubkey: Address,
}


// Whitelist ----------------------------------------------------------------------------------------------------------

/// The most entries that a whitelist can hold (`MAX_WHITELIST_ENTRIES` in `program/inc/whitelist.h`)
pub const MAX_WHITELIST_ENTRIES: usize = 300;

/// The whitelist of a block: the accounts that may buy its entries during its whitelist period.  `Whitelist` in
/// `program/inc/whitelist.h`.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Pod, Zeroable)]
pub struct Whitelist {
    /// [`DataType::Whitelist`]
    pub data_type: u32,
    /// The number of entries in use
    pub count: u16,
    /// The whitelisted accounts; only the first `count` are in use (see [`Whitelist::entries()`])
    pub entries: [Address; MAX_WHITELIST_ENTRIES],
    _padding0: [u8; 2],
}

impl Whitelist {
    /// The whitelisted accounts that are in use
    pub fn entries(&self) -> &[Address] {
        &self.entries[..(self.count as usize).min(MAX_WHITELIST_ENTRIES)]
    }
}


// The sizes of the C structures, as the program's compiler lays them out; the tests check every field's offset too
const _: () = assert!(core::mem::size_of::<ProgramState>() == 36);
const _: () = assert!(core::mem::size_of::<Authority>() == 16);
const _: () = assert!(core::mem::size_of::<BlockConfiguration>() == 64);
const _: () = assert!(core::mem::size_of::<Block>() == 104);
const _: () = assert!(core::mem::size_of::<LevelMetadata>() == 308);
const _: () = assert!(core::mem::size_of::<EntryMetadata>() == 2844);
const _: () = assert!(core::mem::size_of::<Entry>() == 3192);
const _: () = assert!(core::mem::size_of::<Bid>() == 68);
const _: () = assert!(core::mem::size_of::<Whitelist>() == 9608);
