//! A builder for each instruction of the Shinobi Immortals program, for clients that build transactions.
//!
//! Each returns a `solana_instruction::Instruction` holding exactly the accounts (with their permissions) and data that
//! the program expects: the same as the corresponding script in `scripts/` makes, which the tests check, account by
//! account and byte by byte.  The builders are in the order of the instructions' codes.
//!
//! Instructions about an entry take the entry itself (`&Entry`, as read from its account), from which they take the
//! addresses that they need.  Addresses that must be derived (the entry's own address, associated token accounts, and
//! so on) are found with [`crate::pda`], which costs a little time off chain, and a lot of compute units on chain.
//!
//! A builder whose input may be invalid returns `Result<Instruction, BuildError>`; the rest cannot fail.

use crate::id;
use crate::pda;
use crate::state::{BlockConfiguration, Entry};
use alloc::vec::Vec;
use solana_address::Address;
use solana_instruction::{AccountMeta, Instruction};

/// The code of each instruction, which is the first byte of its data (`Instruction` in `program/entrypoint.c`)
#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum InstructionCode {
    /// [`initialize`]
    Initialize = 0,
    /// [`set_admin`]
    SetAdmin = 1,
    /// [`create_block`]
    CreateBlock = 2,
    /// [`add_entries_to_block`]
    AddEntriesToBlock = 3,
    /// [`set_metadata_bytes`]
    SetMetadataBytes = 4,
    /// [`reveal_entries`]
    RevealEntries = 5,
    /// [`add_whitelist_entries`]
    AddWhitelistEntries = 6,
    /// [`delete_whitelist`]
    DeleteWhitelist = 7,
    /// [`withdraw_excess`]
    WithdrawExcess = 8,
    /// [`buy`]
    Buy = 9,
    /// [`refund`]
    Refund = 10,
    /// [`bid`]
    Bid = 11,
    /// [`claim_losing`]
    ClaimLosing = 12,
    /// [`stake`]
    Stake = 13,
    /// [`destake`]
    Destake = 14,
    /// [`thaw`]
    Thaw = 15,
    /// [`harvest`]
    Harvest = 16,
    /// [`level_up`]
    LevelUp = 17,
    /// [`settle_winning_bid`]
    SettleWinningBid = 18,
    /// [`delegate`]
    Delegate = 19,
    /// [`update_epochs_staked`]
    UpdateEpochsStaked = 20,
    /// [`reauthorize`]
    Reauthorize = 21,
}


/// The ways in which building an instruction can fail
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum BuildError {
    /// The Metaplex metadata URI is longer than [`MAX_URI_LENGTH`] bytes
    UriTooLong,
    /// The number of whitelist addresses is not from 1 to [`MAX_WHITELIST_ADD_ENTRIES`]
    InvalidWhitelistCount,
    /// More metadata bytes than fit in the instruction's 16 bit count
    TooManyBytes,
    /// More entries than the program can add or reveal at once (more than fit in its 8 bit count)
    TooManyEntries,
}

impl core::fmt::Display for BuildError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.write_str(match self {
            BuildError::UriTooLong => "the uri is longer than 200 bytes",
            BuildError::InvalidWhitelistCount => "from 1 to 28 whitelist addresses may be added at once",
            BuildError::TooManyBytes => "too many metadata bytes for one instruction",
            BuildError::TooManyEntries => "too many entries for one instruction",
        })
    }
}

impl core::error::Error for BuildError {}


/// The longest Metaplex metadata URI, in bytes (the Metaplex maximum)
pub const MAX_URI_LENGTH: usize = 200;

/// The most addresses that one AddWhitelistEntries instruction can add (`MAX_WHITELIST_ADD_ENTRIES` in
/// `program/admin/admin_add_whitelist_entries.c`)
pub const MAX_WHITELIST_ADD_ENTRIES: usize = 28;


/// Returns an account of an instruction
fn account(address: &Address, is_signer: bool, is_writable: bool) -> AccountMeta {
    AccountMeta { pubkey: *address, is_signer, is_writable }
}

/// Returns an instruction of the program
fn instruction(accounts: Vec<AccountMeta>, data: Vec<u8>) -> Instruction {
    Instruction { program_id: id::PROGRAM_ID, accounts, data }
}

/// Returns the address of an entry, derived from its mint
fn entry_address_of(entry: &Entry) -> Address {
    pda::entry_address(&entry.mint_pubkey)
}


// Superuser instructions ---------------------------------------------------------------------------------------------
//
// These can only be performed by the superuser, whose address is compiled into the program.

/// Initialize: initializes the program, creating its program state account (recording `admin` as the admin), its
/// authority account, and the Ki and bid marker mints and their metadata, all paid for by the superuser.  This can be
/// done only once.  (`scripts/super_initialize_tx.sh`)
pub fn initialize(superuser: &Address, admin: &Address) -> Instruction {
    let accounts = alloc::vec![
        // The superuser, who must sign, and who pays for the accounts that are created
        account(superuser, true, true),
        // The program state account, which is created
        account(&id::PROGRAM_STATE, false, true),
        // The program authority account, which is created
        account(&id::AUTHORITY, false, true),
        // The Ki mint and its Metaplex metadata, which are created
        account(&id::KI_MINT, false, true),
        account(&id::KI_METADATA, false, true),
        // The bid marker mint and its Metaplex metadata, which are created
        account(&id::BID_MARKER_MINT, false, true),
        account(&id::BID_MARKER_METADATA, false, true),
        account(&id::RENT_SYSVAR, false, false),
        account(&id::SYSTEM_PROGRAM, false, false),
        account(&id::SPL_TOKEN_PROGRAM, false, false),
        account(&id::METAPLEX_PROGRAM, false, false),
    ];

    // The instruction code and then the admin's address (InitializeData in program/super/super_initialize.c)
    let mut data = alloc::vec![InstructionCode::Initialize as u8];
    data.extend_from_slice(admin.as_ref());

    instruction(accounts, data)
}


/// SetAdmin: changes the admin to `new_admin`.  (`scripts/super_set_admin_tx.sh`)
pub fn set_admin(superuser: &Address, new_admin: &Address) -> Instruction {
    let accounts = alloc::vec![
        // The superuser, who must sign
        account(superuser, true, false),
        // The program state account, which records the admin
        account(&id::PROGRAM_STATE, false, true),
    ];

    // The instruction code and then the new admin's address (UpdateAdminData in program/super/super_set_admin.c)
    let mut data = alloc::vec![InstructionCode::SetAdmin as u8];
    data.extend_from_slice(new_admin.as_ref());

    instruction(accounts, data)
}


// Admin instructions -------------------------------------------------------------------------------------------------
//
// These can only be performed by the admin, whose address is held in the program state account.  Where a `funding`
// account is taken, it pays for the accounts that are created, and must also sign; if it is `None`, the admin pays.

/// CreateBlock: creates the block `config.block_number` of group `config.group_number`, with the configuration
/// `config` (see [`BlockConfiguration`]; make one with `bytemuck::Zeroable::zeroed()` and set its fields).  If its
/// `whitelist_duration` is nonzero, the block's whitelist must already exist and not be empty.
/// (`scripts/admin_create_block_tx.sh`)
pub fn create_block(admin: &Address, config: &BlockConfiguration, funding: Option<&Address>) -> Instruction {
    let funding = funding.unwrap_or(admin);
    let block = pda::block_address(config.group_number, config.block_number);

    let accounts = alloc::vec![
        // The program state account, which identifies the admin
        account(&id::PROGRAM_STATE, false, false),
        // The admin, who must sign
        account(admin, true, false),
        // The funding account, which pays for the block account
        account(funding, true, true),
        // The block account, which is created
        account(&block, false, true),
        account(&id::SYSTEM_PROGRAM, false, false),
        // The block's whitelist, which is used only if the block has a whitelist period
        account(&pda::whitelist_address(&block), false, false),
    ];

    // The instruction code, 7 bytes of padding (CreateBlockData in program/admin/admin_create_block.c aligns the
    // configuration to 8 bytes), and then the BlockConfiguration, with its padding as zeroes
    let mut data = alloc::vec![InstructionCode::CreateBlock as u8, 0, 0, 0, 0, 0, 0, 0];
    data.extend_from_slice(&config.group_number.to_le_bytes());
    data.extend_from_slice(&config.block_number.to_le_bytes());
    data.extend_from_slice(&config.total_entry_count.to_le_bytes());
    data.extend_from_slice(&config.total_mystery_count.to_le_bytes());
    data.extend_from_slice(&config.mystery_phase_duration.to_le_bytes());
    data.extend_from_slice(&config.mystery_start_price_lamports.to_le_bytes());
    data.extend_from_slice(&config.reveal_period_duration.to_le_bytes());
    data.extend_from_slice(&[0; 4]);
    data.extend_from_slice(&config.minimum_price_lamports.to_le_bytes());
    data.push(config.has_auction() as u8);
    data.extend_from_slice(&[0; 3]);
    data.extend_from_slice(&config.duration.to_le_bytes());
    data.extend_from_slice(&config.final_start_price_lamports.to_le_bytes());
    data.extend_from_slice(&config.whitelist_duration.to_le_bytes());
    data.extend_from_slice(&[0; 4]);

    instruction(accounts, data)
}


/// AddEntriesToBlock: adds entries to the block `block_number` of group `group_number`, starting at index
/// `first_entry_index`, one for each of `entry_sha256s`, each of which is the SHA-256 of the entry's metadata and salt
/// (checked when the entry is revealed).  `metaplex_metadata_uri` is the initial URI of each entry's Metaplex metadata,
/// at most [`MAX_URI_LENGTH`] bytes; `second_creator` is an additional creator for each entry's Metaplex metadata, if
/// any.  Each entry adds 4 accounts and 32 bytes to the transaction, which limits how many can be added at once.
/// (`scripts/admin_add_entries_to_block_tx.sh`)
#[allow(clippy::too_many_arguments)]
pub fn add_entries_to_block(admin: &Address, group_number: u32, block_number: u32, metaplex_metadata_uri: &str,
                            second_creator: Option<&Address>, first_entry_index: u16, entry_sha256s: &[[u8; 32]],
                            funding: Option<&Address>) -> Result<Instruction, BuildError> {
    if metaplex_metadata_uri.len() > MAX_URI_LENGTH {
        return Err(BuildError::UriTooLong);
    }
    if entry_sha256s.len() > (u8::MAX as usize) {
        return Err(BuildError::TooManyEntries);
    }
    let funding = funding.unwrap_or(admin);
    // The program treats a second creator of all zeroes (the system program's address) as none
    let second_creator = second_creator.unwrap_or(&id::SYSTEM_PROGRAM);
    let block = pda::block_address(group_number, block_number);

    let mut accounts = alloc::vec![
        // The program state account, which identifies the admin
        account(&id::PROGRAM_STATE, false, false),
        // The admin, who must sign
        account(admin, true, false),
        // The funding account, which pays for the accounts of each entry
        account(funding, true, true),
        // The block
        account(&block, false, true),
        account(&id::AUTHORITY, false, false),
        account(&id::SYSTEM_PROGRAM, false, false),
        account(&id::SPL_TOKEN_PROGRAM, false, false),
        account(&id::METAPLEX_PROGRAM, false, false),
        account(&id::RENT_SYSVAR, false, false),
    ];

    for i in 0..entry_sha256s.len() {
        let mint = pda::entry_mint_address(&block, first_entry_index.wrapping_add(i as u16));
        // The entry, its mint, its own token account, and its Metaplex metadata, which are created
        accounts.push(account(&pda::entry_address(&mint), false, true));
        accounts.push(account(&mint, false, true));
        accounts.push(account(&pda::entry_token_address(&mint), false, true));
        accounts.push(account(&pda::metaplex_metadata_address(&mint), false, true));
    }

    // AddEntriesToBlockData in program/admin/admin_add_entries_to_block.c: the instruction code, the zero padded uri,
    // the second creator, a byte of padding to align the u16 that follows, the first entry's index, and the SHA-256
    // of each entry
    let mut data = alloc::vec![InstructionCode::AddEntriesToBlock as u8];
    data.extend_from_slice(metaplex_metadata_uri.as_bytes());
    data.resize(1 + MAX_URI_LENGTH, 0);
    data.extend_from_slice(second_creator.as_ref());
    data.push(0);
    data.extend_from_slice(&first_entry_index.to_le_bytes());
    for sha256 in entry_sha256s {
        data.extend_from_slice(sha256);
    }

    Ok(instruction(accounts, data))
}


/// SetMetadataBytes: sets `bytes` of the metadata ([`crate::EntryMetadata`]) of entry `entry_index` of the block
/// `block_number` of group `group_number`, starting at byte `start_index` of the metadata.  This is possible only
/// before the entry is revealed.  (`scripts/admin_set_metadata_bytes_tx.sh`)
pub fn set_metadata_bytes(admin: &Address, group_number: u32, block_number: u32, entry_index: u16, start_index: u16,
                          bytes: &[u8]) -> Result<Instruction, BuildError> {
    let count = u16::try_from(bytes.len()).map_err(|_| BuildError::TooManyBytes)?;
    let block = pda::block_address(group_number, block_number);
    let entry = pda::entry_address(&pda::entry_mint_address(&block, entry_index));

    let accounts = alloc::vec![
        // The program state account, which identifies the admin
        account(&id::PROGRAM_STATE, false, false),
        // The admin, who must sign
        account(admin, true, false),
        account(&block, false, false),
        // The entry, whose metadata is set
        account(&entry, false, true),
    ];

    // SetMetadataBytesData in program/admin/admin_set_metadata_bytes.c: the instruction code, a byte of padding, the
    // start index, the number of bytes, and the bytes
    let mut data = alloc::vec![InstructionCode::SetMetadataBytes as u8, 0];
    data.extend_from_slice(&start_index.to_le_bytes());
    data.extend_from_slice(&count.to_le_bytes());
    data.extend_from_slice(bytes);

    Ok(instruction(accounts, data))
}


/// RevealEntries: reveals entries of the block `block_number` of group `group_number`, starting at index
/// `first_entry_index`, one for each of `salts`, each of which is the salt that was hashed with the entry's metadata to
/// make the SHA-256 that the entry was added with.  The purchase prices of revealed mysteries, held in escrow until
/// now, go to the admin.  Each entry adds 2 accounts and 8 bytes to the transaction.
/// (`scripts/admin_reveal_entries_tx.sh`)
pub fn reveal_entries(admin: &Address, group_number: u32, block_number: u32, first_entry_index: u16, salts: &[u64])
                      -> Result<Instruction, BuildError> {
    if salts.len() > (u8::MAX as usize) {
        return Err(BuildError::TooManyEntries);
    }
    let block = pda::block_address(group_number, block_number);

    let mut accounts = alloc::vec![
        // The program state account, which identifies the admin
        account(&id::PROGRAM_STATE, false, false),
        // The admin, who must sign, and who receives the escrowed purchase prices of revealed mysteries
        account(admin, true, true),
        account(&block, false, true),
        // The program authority, which holds the purchase prices of mysteries in escrow
        account(&id::AUTHORITY, false, true),
        account(&id::SYSTEM_PROGRAM, false, false),
        account(&id::METAPLEX_PROGRAM, false, false),
    ];

    for i in 0..salts.len() {
        let mint = pda::entry_mint_address(&block, first_entry_index.wrapping_add(i as u16));
        // The entry, which is revealed, and its Metaplex metadata, which is updated
        accounts.push(account(&pda::entry_address(&mint), false, true));
        accounts.push(account(&pda::metaplex_metadata_address(&mint), false, true));
    }

    // RevealEntriesData in program/admin/admin_reveal_entries.c: the instruction code, a byte of padding, the first
    // entry's index, 4 bytes of padding to align the u64s that follow, and the salt of each entry
    let mut data = alloc::vec![InstructionCode::RevealEntries as u8, 0];
    data.extend_from_slice(&first_entry_index.to_le_bytes());
    data.extend_from_slice(&[0; 4]);
    for salt in salts {
        data.extend_from_slice(&salt.to_le_bytes());
    }

    Ok(instruction(accounts, data))
}


/// AddWhitelistEntries: adds `addresses` (from 1 to [`MAX_WHITELIST_ADD_ENTRIES`] of them) to the whitelist of the
/// block `block_number` of group `group_number`, creating the whitelist if it does not exist yet.  This is possible
/// only before the block is created.  An address that appears more than once may buy that many entries.
/// (`scripts/admin_add_whitelist_entries_tx.sh`)
pub fn add_whitelist_entries(admin: &Address, group_number: u32, block_number: u32, addresses: &[Address],
                             funding: Option<&Address>) -> Result<Instruction, BuildError> {
    if addresses.is_empty() || (addresses.len() > MAX_WHITELIST_ADD_ENTRIES) {
        return Err(BuildError::InvalidWhitelistCount);
    }
    let funding = funding.unwrap_or(admin);
    let block = pda::block_address(group_number, block_number);

    let accounts = alloc::vec![
        // The program state account, which identifies the admin
        account(&id::PROGRAM_STATE, false, false),
        // The admin, who must sign
        account(admin, true, false),
        // The funding account, which pays for the whitelist account if it is created
        account(funding, true, true),
        // The block, which must not exist yet
        account(&block, false, false),
        // The block's whitelist
        account(&pda::whitelist_address(&block), false, true),
        account(&id::SYSTEM_PROGRAM, false, false),
    ];

    // AddWhitelistEntriesData in program/admin/admin_add_whitelist_entries.c: the instruction code, a byte of padding,
    // the number of addresses, and the addresses
    let mut data = alloc::vec![InstructionCode::AddWhitelistEntries as u8, 0];
    data.extend_from_slice(&(addresses.len() as u16).to_le_bytes());
    for address in addresses {
        data.extend_from_slice(address.as_ref());
    }

    Ok(instruction(accounts, data))
}


/// DeleteWhitelist: deletes the whitelist of the block `block_number` of group `group_number`, returning its lamports
/// to the admin (see `whitelist_duration` in `program/inc/block.h` for when this is allowed).
/// (`scripts/admin_delete_whitelist_tx.sh`)
pub fn delete_whitelist(admin: &Address, group_number: u32, block_number: u32) -> Instruction {
    let block = pda::block_address(group_number, block_number);

    let accounts = alloc::vec![
        // The program state account, which identifies the admin
        account(&id::PROGRAM_STATE, false, false),
        // The admin, who must sign, and who receives the whitelist's lamports
        account(admin, true, true),
        // The block, which need not exist
        account(&block, false, false),
        // The block's whitelist, which is deleted
        account(&pda::whitelist_address(&block), false, true),
    ];

    instruction(accounts, alloc::vec![InstructionCode::DeleteWhitelist as u8])
}


/// The account that [`withdraw_excess`] withdraws from (`WithdrawExcessAccountType` in
/// `program/admin/admin_withdraw_excess.c`)
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WithdrawExcessAccount {
    /// The program state account
    ProgramState,
    /// The authority account.  The escrowed purchase prices of unrevealed mysteries are never withdrawn.
    Authority,
    /// The block account at this address
    Block(Address),
    /// The entry account at this address
    Entry(Address),
    /// The whitelist of the block at this address
    Whitelist(Address),
}

impl WithdrawExcessAccount {
    /// The code by which the program knows the type of account
    pub fn code(&self) -> u8 {
        match self {
            WithdrawExcessAccount::ProgramState => 0,
            WithdrawExcessAccount::Authority => 1,
            WithdrawExcessAccount::Block(_) => 2,
            WithdrawExcessAccount::Entry(_) => 3,
            WithdrawExcessAccount::Whitelist(_) => 4,
        }
    }

    /// The address of the account
    pub fn address(&self) -> Address {
        match self {
            WithdrawExcessAccount::ProgramState => id::PROGRAM_STATE,
            WithdrawExcessAccount::Authority => id::AUTHORITY,
            WithdrawExcessAccount::Block(address) | WithdrawExcessAccount::Entry(address) => *address,
            WithdrawExcessAccount::Whitelist(block) => pda::whitelist_address(block),
        }
    }
}


/// WithdrawExcess: withdraws to the admin the lamports that `from` holds beyond what it must hold (its rent exempt
/// minimum, and for the authority, the escrow).  (`scripts/admin_withdraw_excess_tx.sh`)
pub fn withdraw_excess(admin: &Address, from: &WithdrawExcessAccount) -> Instruction {
    let accounts = alloc::vec![
        // The program state account, which identifies the admin
        account(&id::PROGRAM_STATE, false, false),
        // The admin, who must sign, and who receives the lamports
        account(admin, true, true),
        // The account to withdraw from
        account(&from.address(), false, true),
    ];

    instruction(accounts, alloc::vec![InstructionCode::WithdrawExcess as u8, from.code()])
}


// User instructions --------------------------------------------------------------------------------------------------
//
// These are performed by users: buyers and bidders, and the owners of entries.  The `wallet` is the user, who signs.

/// Buy: buys `entry` for at most `maximum_price_lamports`, paid by `wallet`, whose associated token account for the
/// entry's mint receives the token (and is created if need be).  `admin` is the admin's address, from the program state
/// account.  (`scripts/user_buy_tx.sh`)
pub fn buy(entry: &Entry, maximum_price_lamports: u64, wallet: &Address, admin: &Address) -> Instruction {
    let accounts = alloc::vec![
        // The funding account, which pays the purchase price
        account(wallet, true, true),
        // The program state account, which identifies the admin
        account(&id::PROGRAM_STATE, false, false),
        // The admin, who receives the purchase price of a non-mystery
        account(admin, false, true),
        // The program authority, which holds the purchase price of a mystery in escrow
        account(&id::AUTHORITY, false, true),
        account(&entry.block_pubkey, false, true),
        // The block's whitelist, whose entry for the wallet is used up during the block's whitelist period
        account(&pda::whitelist_address(&entry.block_pubkey), false, true),
        account(&entry_address_of(entry), false, true),
        // The entry's own token account, which holds the token until now, and is closed
        account(&entry.token_pubkey, false, true),
        account(&entry.mint_pubkey, false, false),
        // The wallet's associated token account for the entry's mint, which receives the token
        account(&pda::associated_token_address(wallet, &entry.mint_pubkey), false, true),
        // The owner of that token account
        account(wallet, false, false),
        account(&id::PROGRAM_ID, false, false),
        account(&id::SPL_TOKEN_PROGRAM, false, false),
        account(&id::SPL_ASSOCIATED_TOKEN_PROGRAM, false, false),
        account(&id::SYSTEM_PROGRAM, false, false),
    ];

    // The instruction code, 7 bytes of padding (BuyData aligns its u64), and the maximum price
    let mut data = alloc::vec![InstructionCode::Buy as u8, 0, 0, 0, 0, 0, 0, 0];
    data.extend_from_slice(&maximum_price_lamports.to_le_bytes());

    instruction(accounts, data)
}


/// Refund: refunds the purchase price of `entry`, bought as a mystery and not revealed within its block's reveal
/// period, to `wallet`, which owns the token (held in `token`, or if `None`, in `wallet`'s associated token account
/// for the entry's mint).  The token is kept.  (`scripts/user_refund_tx.sh`)
pub fn refund(entry: &Entry, wallet: &Address, token: Option<&Address>) -> Instruction {
    let token = token.copied().unwrap_or_else(|| pda::associated_token_address(wallet, &entry.mint_pubkey));

    let accounts = alloc::vec![
        // The token owner, who must sign
        account(wallet, true, false),
        account(&entry.block_pubkey, false, false),
        account(&entry_address_of(entry), false, true),
        // The program authority, which holds the purchase price in escrow and pays the refund
        account(&id::AUTHORITY, false, true),
        // The token account holding the entry's token
        account(&token, false, false),
        // The account that receives the refund
        account(wallet, false, true),
    ];

    instruction(accounts, alloc::vec![InstructionCode::Refund as u8])
}


/// Bid: bids on `entry`, in auction, from `wallet`: at least `minimum_bid_lamports` (a bid that would be less fails,
/// rather than being raised), and as much as needed to be the highest bid, up to `maximum_bid_lamports`.
/// (`scripts/user_bid_tx.sh`)
pub fn bid(entry: &Entry, minimum_bid_lamports: u64, maximum_bid_lamports: u64, wallet: &Address) -> Instruction {
    let bid_marker_token = pda::bid_marker_token_address(&entry.mint_pubkey, wallet);

    let accounts = alloc::vec![
        // The bidding account, which pays the bid
        account(wallet, true, true),
        account(&entry_address_of(entry), false, true),
        account(&id::BID_MARKER_MINT, false, true),
        // The bidder's bid marker token account for the entry
        account(&bid_marker_token, false, true),
        // The bid account, which holds the bid
        account(&pda::bid_address(&bid_marker_token), false, true),
        account(&id::AUTHORITY, false, false),
        account(&id::SYSTEM_PROGRAM, false, false),
        account(&id::PROGRAM_ID, false, false),
        account(&id::SPL_TOKEN_PROGRAM, false, false),
    ];

    // The instruction code, 7 bytes of padding (BidData aligns its u64s), and the minimum and maximum bids
    let mut data = alloc::vec![InstructionCode::Bid as u8, 0, 0, 0, 0, 0, 0, 0];
    data.extend_from_slice(&minimum_bid_lamports.to_le_bytes());
    data.extend_from_slice(&maximum_bid_lamports.to_le_bytes());

    instruction(accounts, data)
}


/// ClaimLosing: returns `wallet`'s losing bid on `entry` (whose auction is over).  The bid marker is reclaimed too
/// (its tokens burned and its token account closed), unless `keep_bid_marker`, which is needed only if the bidder has
/// already burned or closed it.  (`scripts/user_claim_losing_tx.sh`)
pub fn claim_losing(entry: &Entry, wallet: &Address, keep_bid_marker: bool) -> Instruction {
    let bid_marker_token = pda::bid_marker_token_address(&entry.mint_pubkey, wallet);

    let mut accounts = alloc::vec![
        // The bidding account, which receives the bid back
        account(wallet, true, true),
        account(&entry_address_of(entry), false, false),
        // The bid account, which is closed
        account(&pda::bid_address(&bid_marker_token), false, true),
    ];

    if !keep_bid_marker {
        accounts.push(account(&id::BID_MARKER_MINT, false, true));
        accounts.push(account(&bid_marker_token, false, true));
        accounts.push(account(&id::AUTHORITY, false, false));
        accounts.push(account(&id::SPL_TOKEN_PROGRAM, false, false));
    }

    instruction(accounts, alloc::vec![InstructionCode::ClaimLosing as u8])
}


/// Stake: stakes `entry` with the stake account `stake_account`, which is delegated to Shinobi Systems and whose
/// authorities pass to the program.  `wallet` must own the entry's token, in its associated token account (which is
/// frozen while the entry is staked), and be the stake account's withdraw authority.  (`scripts/user_stake_tx.sh`)
pub fn stake(entry: &Entry, stake_account: &Address, wallet: &Address) -> Instruction {
    let accounts = alloc::vec![
        account(&entry.block_pubkey, false, false),
        account(&entry_address_of(entry), false, true),
        // The token owner
        account(wallet, true, false),
        // The token account holding the entry's token, which is frozen
        account(&pda::associated_token_address(wallet, &entry.mint_pubkey), false, true),
        account(stake_account, false, true),
        // The withdraw authority of the stake account
        account(wallet, true, false),
        account(&id::SHINOBI_SYSTEMS_VOTE, false, false),
        // The program authority, which becomes the stake account's authority
        account(&id::AUTHORITY, false, false),
        account(&id::CLOCK_SYSVAR, false, false),
        account(&id::STAKE_PROGRAM, false, false),
        account(&id::STAKE_CONFIG, false, false),
        account(&id::STAKE_HISTORY_SYSVAR, false, false),
        // The entry's mint, needed to freeze the token account
        account(&entry.mint_pubkey, false, false),
        account(&id::SPL_TOKEN_PROGRAM, false, false),
    ];

    instruction(accounts, alloc::vec![InstructionCode::Stake as u8])
}


/// Destake: returns the stake account of `entry` to `wallet`, which owns the entry's token, harvesting its Ki (unless
/// `no_ki_harvest`) and thawing its token account (unless `no_thaw`).  The flags exist so that a stake account can
/// always be recovered, even if harvesting or thawing were ever to fail.  (`scripts/user_destake_tx.sh`)
pub fn destake(entry: &Entry, wallet: &Address, no_ki_harvest: bool, no_thaw: bool) -> Instruction {
    let accounts = alloc::vec![
        // The funding account, which pays for the Ki destination account if it does not exist yet
        account(wallet, true, true),
        account(&entry.block_pubkey, false, false),
        account(&entry_address_of(entry), false, true),
        // The token owner, who receives the stake account
        account(wallet, true, false),
        // The token account holding the entry's token, which is thawed unless no_thaw
        account(&pda::associated_token_address(wallet, &entry.mint_pubkey), false, true),
        account(&entry.owned.stake_account, false, true),
        // The Ki destination account and its owner
        account(&pda::associated_token_address(wallet, &id::KI_MINT), false, true),
        account(wallet, false, false),
        account(&id::KI_MINT, false, true),
        account(&id::AUTHORITY, false, false),
        account(&id::CLOCK_SYSVAR, false, false),
        account(&id::SYSTEM_PROGRAM, false, false),
        account(&id::STAKE_PROGRAM, false, false),
        account(&id::SPL_TOKEN_PROGRAM, false, false),
        account(&id::SPL_ASSOCIATED_TOKEN_PROGRAM, false, false),
        // The entry's mint, needed to thaw the token account
        account(&entry.mint_pubkey, false, false),
    ];

    // The instruction code, then the no_ki_harvest flag, then the no_thaw flag
    instruction(accounts, alloc::vec![InstructionCode::Destake as u8, no_ki_harvest as u8, no_thaw as u8])
}


/// Thaw: thaws `wallet`'s token account for `entry`, if it was left frozen by a Destake with `no_thaw`.  Only possible
/// while the entry is not staked.  (`scripts/user_thaw_tx.sh`)
pub fn thaw(entry: &Entry, wallet: &Address) -> Instruction {
    let accounts = alloc::vec![
        account(&entry.block_pubkey, false, false),
        account(&entry_address_of(entry), false, false),
        // The token owner
        account(wallet, true, false),
        // The token account holding the entry's token, which is thawed
        account(&pda::associated_token_address(wallet, &entry.mint_pubkey), false, true),
        account(&entry.mint_pubkey, false, false),
        // The program authority, the freeze authority of the entry's mint
        account(&id::AUTHORITY, false, false),
        account(&id::SPL_TOKEN_PROGRAM, false, false),
    ];

    instruction(accounts, alloc::vec![InstructionCode::Thaw as u8])
}


/// Harvest: harvests the Ki earned by the stake account of `entry` into `wallet`'s associated Ki token account
/// (created if need be).  `wallet` must own the entry's token.  (`scripts/user_harvest_tx.sh`)
pub fn harvest(entry: &Entry, wallet: &Address) -> Instruction {
    let accounts = alloc::vec![
        // The funding account, which pays for the Ki destination account if it does not exist yet
        account(wallet, true, true),
        account(&entry_address_of(entry), false, true),
        // The token owner
        account(wallet, true, false),
        // The token account holding the entry's token
        account(&pda::associated_token_address(wallet, &entry.mint_pubkey), false, false),
        account(&entry.owned.stake_account, false, false),
        // The Ki destination account and its owner
        account(&pda::associated_token_address(wallet, &id::KI_MINT), false, true),
        account(wallet, false, false),
        account(&id::KI_MINT, false, true),
        account(&id::AUTHORITY, false, false),
        account(&id::SYSTEM_PROGRAM, false, false),
        account(&id::SPL_TOKEN_PROGRAM, false, false),
        account(&id::SPL_ASSOCIATED_TOKEN_PROGRAM, false, false),
        // The entry's block, whose block_start_timestamp begins the entry's life span
        account(&entry.block_pubkey, false, false),
    ];

    instruction(accounts, alloc::vec![InstructionCode::Harvest as u8])
}


/// LevelUp: applies up to `ki_to_apply` whole Ki from `wallet`'s associated Ki token account (only as much as earns
/// level credits is burned) to `entry`, levelling it up as far as its level credits allow.  `wallet` must own the
/// entry's token.  (`scripts/user_level_up_tx.sh`)
pub fn level_up(entry: &Entry, ki_to_apply: u64, wallet: &Address) -> Instruction {
    let accounts = alloc::vec![
        account(&entry_address_of(entry), false, true),
        // The token owner
        account(wallet, true, false),
        // The token account holding the entry's token
        account(&pda::associated_token_address(wallet, &entry.mint_pubkey), false, false),
        // The entry's Metaplex metadata, which is updated to the new level
        account(&entry.metaplex_metadata_pubkey, false, true),
        // The Ki source account, from which Ki is burned
        account(&pda::associated_token_address(wallet, &id::KI_MINT), false, true),
        // The owner of the Ki source account, which pays for it if it has to be created
        account(wallet, true, true),
        account(&id::KI_MINT, false, true),
        account(&id::AUTHORITY, false, false),
        account(&id::SPL_TOKEN_PROGRAM, false, false),
        account(&id::METAPLEX_PROGRAM, false, false),
        // The entry's stake account; all zeroes (the system program's address) if it is not staked, which is the
        // placeholder that LevelUp expects
        account(&entry.owned.stake_account, false, false),
        account(&id::SYSTEM_PROGRAM, false, false),
        account(&id::SPL_ASSOCIATED_TOKEN_PROGRAM, false, false),
        // The entry's block, used only if the entry is staked
        account(&entry.block_pubkey, false, false),
    ];

    // The instruction code, 7 bytes of padding (LevelUpData aligns its u64), and the most Ki to apply
    let mut data = alloc::vec![InstructionCode::LevelUp as u8, 0, 0, 0, 0, 0, 0, 0];
    data.extend_from_slice(&ki_to_apply.to_le_bytes());

    instruction(accounts, data)
}


// Anyone instructions ------------------------------------------------------------------------------------------------
//
// These can be performed by anyone (SettleWinningBid only by the winning bidder until 30 days after the auction).

/// SettleWinningBid: settles the winning bid, by `bidder`, of `entry`, whose auction is over: the bid goes to `admin`
/// (from the program state account), and the token to `bidder`'s associated token account.  `wallet` pays for that
/// token account if it must be created.  If `wallet` is the bidder, the bid marker is reclaimed too, unless
/// `keep_bid_marker`.  (`scripts/anyone_settle_winning_bid_tx.sh`)
pub fn settle_winning_bid(entry: &Entry, bidder: &Address, admin: &Address, wallet: &Address, keep_bid_marker: bool)
                          -> Instruction {
    let bid_marker_token = pda::bid_marker_token_address(&entry.mint_pubkey, bidder);

    let mut accounts = alloc::vec![
        // The funding account
        account(wallet, true, true),
        account(&entry_address_of(entry), false, true),
        // The winning bid account
        account(&pda::bid_address(&bid_marker_token), false, true),
        account(&id::PROGRAM_STATE, false, false),
        // The admin, who receives the winning bid
        account(admin, false, true),
        // The entry's own token account, which holds the token until now, and is closed
        account(&entry.token_pubkey, false, true),
        account(&entry.mint_pubkey, false, false),
        account(&id::AUTHORITY, false, false),
        // The bidder's associated token account for the entry's mint, which receives the token
        account(&pda::associated_token_address(bidder, &entry.mint_pubkey), false, true),
        // The bidder, who receives any lamports of the bid account beyond the bid
        account(bidder, false, true),
        account(&id::SYSTEM_PROGRAM, false, false),
        account(&id::SPL_TOKEN_PROGRAM, false, false),
        account(&id::SPL_ASSOCIATED_TOKEN_PROGRAM, false, false),
    ];

    // Only the bidder can reclaim their bid marker, since it needs their signature
    if (wallet == bidder) && !keep_bid_marker {
        accounts.push(account(&id::BID_MARKER_MINT, false, true));
        accounts.push(account(&bid_marker_token, false, true));
    }

    instruction(accounts, alloc::vec![InstructionCode::SettleWinningBid as u8])
}


/// Delegate: delegates the stake account of `entry` to Shinobi Systems again, if it has been deactivated (or delegated
/// elsewhere).  (`scripts/anyone_delegate_tx.sh`)
pub fn delegate(entry: &Entry) -> Instruction {
    let accounts = alloc::vec![
        account(&entry.block_pubkey, false, false),
        account(&entry_address_of(entry), false, true),
        account(&entry.owned.stake_account, false, true),
        // The program authority, the stake account's stake authority
        account(&id::AUTHORITY, false, false),
        account(&id::CLOCK_SYSVAR, false, false),
        account(&id::STAKE_PROGRAM, false, false),
        account(&id::STAKE_HISTORY_SYSVAR, false, false),
        account(&id::SHINOBI_SYSTEMS_VOTE, false, false),
        account(&id::STAKE_CONFIG, false, false),
    ];

    instruction(accounts, alloc::vec![InstructionCode::Delegate as u8])
}


/// UpdateEpochsStaked: brings the `epochs_staked` of `entry` up to date.  A transaction can include this before
/// another program's instruction (a game's, say), after which that program can rely on `epochs_staked` being exact
/// (see [`Entry::epochs_staked_is_current`]).  (`scripts/anyone_update_epochs_staked_tx.sh`)
pub fn update_epochs_staked(entry: &Entry) -> Instruction {
    let accounts = alloc::vec![
        account(&entry_address_of(entry), false, true),
        // The entry's stake account; all zeroes (the system program's address) if it is not staked, which
        // UpdateEpochsStaked does not use
        account(&entry.owned.stake_account, false, false),
    ];

    instruction(accounts, alloc::vec![InstructionCode::UpdateEpochsStaked as u8])
}


// Special instructions -----------------------------------------------------------------------------------------------

/// ReAuthorize: moves `entry` to being managed by another program, which needs the signatures of both `admin` and
/// `token_owner` (who owns the entry's token, held in `token`, or if `None`, in their associated token account).
///
/// If the entry is staked, its stake account is returned to `token_owner`, and its token account thawed, as Destake
/// would (but without harvesting Ki; a Harvest instruction may come first).  If `new_metadata_authority` is given,
/// the entry's Metaplex metadata update authority and its mint's freeze authority pass to it, and the entry is
/// retired from this program.  If it is `None`, only the stake account is returned (so the entry must be staked); this
/// exists so that a stake account can be recovered even if the Metaplex program were ever to fail.
/// (`scripts/special_reauthorize_tx.sh`)
pub fn reauthorize(entry: &Entry, admin: &Address, token_owner: &Address, new_metadata_authority: Option<&Address>,
                   token: Option<&Address>) -> Instruction {
    let token = token.copied().unwrap_or_else(|| pda::associated_token_address(token_owner, &entry.mint_pubkey));

    let accounts = alloc::vec![
        // The program state account, which identifies the admin
        account(&id::PROGRAM_STATE, false, false),
        // The admin, who must sign
        account(admin, true, false),
        account(&entry_address_of(entry), false, true),
        // The token owner, who must sign, and who receives the stake account if the entry is staked
        account(token_owner, true, false),
        // The token account holding the entry's token, which is thawed if the entry is staked
        account(&token, false, true),
        // The entry's Metaplex metadata, whose update authority is changed
        account(&entry.metaplex_metadata_pubkey, false, true),
        // The entry's stake account; all zeroes if it is not staked, which the program ignores
        account(&entry.owned.stake_account, false, true),
        account(&id::AUTHORITY, false, false),
        account(&id::CLOCK_SYSVAR, false, false),
        account(&id::METAPLEX_PROGRAM, false, false),
        account(&id::STAKE_PROGRAM, false, false),
        // The entry's mint, whose freeze authority passes with the metadata authority
        account(&entry.mint_pubkey, false, true),
        account(&id::SPL_TOKEN_PROGRAM, false, false),
    ];

    // ReauthorizeData in program/special/special_reauthorize.c: the instruction code, whether to reauthorize the
    // metadata, and the new metadata authority (ignored by the program if not reauthorizing it)
    let mut data = alloc::vec![InstructionCode::Reauthorize as u8, new_metadata_authority.is_some() as u8];
    data.extend_from_slice(new_metadata_authority.unwrap_or(&id::SYSTEM_PROGRAM).as_ref());

    instruction(accounts, data)
}
