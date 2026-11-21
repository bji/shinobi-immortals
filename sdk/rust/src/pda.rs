//! The addresses of the program's accounts, which are program derived addresses.
//!
//! Each `find_*` function returns an address and its bump seed; the function of the same name without `find_` returns
//! only the address.  Finding a program derived address is expensive on chain (each attempt is a
//! `create_program_address` syscall), and is rarely needed there: an account owned by the program, of the right data
//! type, is genuine wherever it is (see [`crate::load`]).  These are mainly for clients.
//!
//! The seeds are those of the program (and of the scripts in `scripts/`); the first seed of each of the program's own
//! addresses is a single byte that identifies the kind of account.

use crate::id;
use solana_address::Address;

/// The first seed of the program state account's address: `[1]`
pub const PROGRAM_STATE_SEED: u8 = 1;
/// The first seed of the authority account's address: `[2]`
pub const AUTHORITY_SEED: u8 = 2;
/// The first seed of the Ki mint's address: `[3]`
pub const KI_MINT_SEED: u8 = 3;
/// The first seed of an entry's mint's address: `[4]`
pub const ENTRY_MINT_SEED: u8 = 4;
/// The first seed of an entry's own token account's address: `[5]`
pub const ENTRY_TOKEN_SEED: u8 = 5;
/// The first seed of a bid account's address: `[6]`
pub const BID_SEED: u8 = 6;
/// The first seed of the bid marker mint's address: `[7]`
pub const BID_MARKER_MINT_SEED: u8 = 7;
/// The first seed of a bid marker token account's address: `[8]`
pub const BID_MARKER_TOKEN_SEED: u8 = 8;
/// The first seed of a whitelist's address: `[9]`
pub const WHITELIST_SEED: u8 = 9;
/// The first seed of a block's address: `[10]`
pub const BLOCK_SEED: u8 = 10;
/// The first seed of an entry's address: `[11]`
pub const ENTRY_SEED: u8 = 11;
/// The seed of the reference stake account ([`crate::id::REFERENCE_STAKE`])
pub const REFERENCE_STAKE_SEED: u8 = 12;


/// Finds the address and bump seed of the block `block_number` of group `group_number`
pub fn find_block_address(group_number: u32, block_number: u32) -> (Address, u8) {
    Address::find_program_address(&[ &[ BLOCK_SEED ], &group_number.to_le_bytes(), &block_number.to_le_bytes() ],
                                  &id::PROGRAM_ID)
}

/// Returns the address of the block `block_number` of group `group_number`
pub fn block_address(group_number: u32, block_number: u32) -> Address {
    find_block_address(group_number, block_number).0
}


/// Finds the address and bump seed of the whitelist of the block at `block`
pub fn find_whitelist_address(block: &Address) -> (Address, u8) {
    Address::find_program_address(&[ &[ WHITELIST_SEED ], block.as_ref() ], &id::PROGRAM_ID)
}

/// Returns the address of the whitelist of the block at `block`
pub fn whitelist_address(block: &Address) -> Address {
    find_whitelist_address(block).0
}


/// Finds the address and bump seed of the mint of entry `entry_index` of the block at `block`
pub fn find_entry_mint_address(block: &Address, entry_index: u16) -> (Address, u8) {
    Address::find_program_address(&[ &[ ENTRY_MINT_SEED ], block.as_ref(), &entry_index.to_le_bytes() ],
                                  &id::PROGRAM_ID)
}

/// Returns the address of the mint of entry `entry_index` of the block at `block`
pub fn entry_mint_address(block: &Address, entry_index: u16) -> Address {
    find_entry_mint_address(block, entry_index).0
}


/// Finds the address and bump seed of the entry whose mint is `mint`
pub fn find_entry_address(mint: &Address) -> (Address, u8) {
    Address::find_program_address(&[ &[ ENTRY_SEED ], mint.as_ref() ], &id::PROGRAM_ID)
}

/// Returns the address of the entry whose mint is `mint`
pub fn entry_address(mint: &Address) -> Address {
    find_entry_address(mint).0
}


/// Finds the address and bump seed of the own token account of the entry whose mint is `mint`, which holds the token
/// until the entry is bought or its auction is settled
pub fn find_entry_token_address(mint: &Address) -> (Address, u8) {
    Address::find_program_address(&[ &[ ENTRY_TOKEN_SEED ], mint.as_ref() ], &id::PROGRAM_ID)
}

/// Returns the address of the own token account of the entry whose mint is `mint`
pub fn entry_token_address(mint: &Address) -> Address {
    find_entry_token_address(mint).0
}


/// Finds the address and bump seed of `bidder`'s bid marker token account for the entry whose mint is `mint`
pub fn find_bid_marker_token_address(mint: &Address, bidder: &Address) -> (Address, u8) {
    Address::find_program_address(&[ &[ BID_MARKER_TOKEN_SEED ], mint.as_ref(), bidder.as_ref() ], &id::PROGRAM_ID)
}

/// Returns the address of `bidder`'s bid marker token account for the entry whose mint is `mint`
pub fn bid_marker_token_address(mint: &Address, bidder: &Address) -> Address {
    find_bid_marker_token_address(mint, bidder).0
}


/// Finds the address and bump seed of the bid account of the bid marker token account at `bid_marker_token`
pub fn find_bid_address(bid_marker_token: &Address) -> (Address, u8) {
    Address::find_program_address(&[ &[ BID_SEED ], bid_marker_token.as_ref() ], &id::PROGRAM_ID)
}

/// Returns the address of the bid account of the bid marker token account at `bid_marker_token`
pub fn bid_address(bid_marker_token: &Address) -> Address {
    find_bid_address(bid_marker_token).0
}


/// Finds the address and bump seed of the Metaplex metadata of the mint `mint`
pub fn find_metaplex_metadata_address(mint: &Address) -> (Address, u8) {
    Address::find_program_address(&[ b"metadata", id::METAPLEX_PROGRAM.as_ref(), mint.as_ref() ],
                                  &id::METAPLEX_PROGRAM)
}

/// Returns the address of the Metaplex metadata of the mint `mint`
pub fn metaplex_metadata_address(mint: &Address) -> Address {
    find_metaplex_metadata_address(mint).0
}


/// Finds the address and bump seed of `owner`'s associated token account for the mint `mint` (of the SPL Token
/// program)
pub fn find_associated_token_address(owner: &Address, mint: &Address) -> (Address, u8) {
    Address::find_program_address(&[ owner.as_ref(), id::SPL_TOKEN_PROGRAM.as_ref(), mint.as_ref() ],
                                  &id::SPL_ASSOCIATED_TOKEN_PROGRAM)
}

/// Returns the address of `owner`'s associated token account for the mint `mint`
pub fn associated_token_address(owner: &Address, mint: &Address) -> Address {
    find_associated_token_address(owner, mint).0
}
