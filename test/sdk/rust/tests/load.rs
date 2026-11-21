// Tests the Rust crate's checked loading and verification (sdk/rust/src/load.rs, account_info.rs, and
// account_view.rs), and the methods of its account types (sdk/rust/src/state.rs).
//
// Each check of loading and verification is made three ways: with the raw account parts (load.rs), with the standard
// Solana crates' AccountInfo (account_info.rs), and with Pinocchio's AccountView (account_view.rs), which must all
// agree.  Genuine accounts must be accepted, and each kind of forged or unsuitable account rejected with its error.

mod common;

use common::{make_entry, Rng};
use shinobi_immortals::load::{self, Error, TOKEN_ACCOUNT_LEN};
use shinobi_immortals::state::{EntryState, MAX_WHITELIST_ENTRIES};
use shinobi_immortals::{account_info, account_view, id, Address, Block, Entry, ProgramState, Whitelist};
use solana_account_view::{AccountView, RuntimeAccount, NOT_BORROWED};
use solana_program_error::ProgramError;

/// An account, as the tests make them: its address, owner, and data
#[derive(Clone)]
struct Account {
    address: Address,
    owner: Address,
    data: Vec<u8>,
}

/// Returns `data` in a buffer of u64s, so that it is 8 byte aligned as account data passed to a program is, with
/// `offset` bytes before it (to misalign it)
fn aligned(data: &[u8], offset: usize) -> Vec<u64> {
    let mut buffer = vec![ 0u64; (offset + data.len() + 7) / 8 ];
    bytemuck::cast_slice_mut::<u64, u8>(&mut buffer)[offset..(offset + data.len())].copy_from_slice(data);
    buffer
}

/// Calls `f` with an AccountInfo of `account`
fn with_account_info<R>(account: &Account, f: impl FnOnce(&solana_account_info::AccountInfo) -> R) -> R {
    let mut buffer = aligned(&account.data, 0);
    let data = &mut bytemuck::cast_slice_mut::<u64, u8>(&mut buffer)[..account.data.len()];
    let mut lamports = 1_000_000;
    let info = solana_account_info::AccountInfo::new(&account.address, false, false, &mut lamports, data,
                                                     &account.owner, false);
    f(&info)
}

/// Calls `f` with a Pinocchio AccountView of `account`, laid out in memory as the runtime lays out accounts: the
/// RuntimeAccount header, immediately followed by the data
fn with_account_view<R>(account: &Account, f: impl FnOnce(&AccountView) -> R) -> R {
    let header = std::mem::size_of::<RuntimeAccount>();
    let mut buffer = vec![ 0u64; (header + account.data.len() + 7) / 8 ];
    let bytes = bytemuck::cast_slice_mut::<u64, u8>(&mut buffer);
    bytes[header..(header + account.data.len())].copy_from_slice(&account.data);
    let raw = bytes.as_mut_ptr() as *mut RuntimeAccount;
    unsafe {
        (*raw).borrow_state = NOT_BORROWED;
        (*raw).address = account.address;
        (*raw).owner = account.owner;
        (*raw).lamports = 1_000_000;
        (*raw).data_len = account.data.len() as u64;
        f(&AccountView::new_unchecked(raw))
    }
}


/// Returns an entry account holding `entry`, as the program would make it
fn entry_account(entry: &Entry) -> Account {
    Account { address: shinobi_immortals::pda::entry_address(&entry.mint_pubkey), owner: id::PROGRAM_ID,
              data: bytemuck::bytes_of(entry).to_vec() }
}

/// Returns the account of the block of `entry`, as the program would make it
fn block_account(entry: &Entry) -> Account {
    let mut block: Block = bytemuck::Zeroable::zeroed();
    block.data_type = 2;
    block.config.group_number = entry.group_number;
    block.config.block_number = entry.block_number;
    block.block_start_timestamp = 1_700_000_000;
    let mut data = bytemuck::bytes_of(&block).to_vec();
    // The entries_added_bitmap that follows the block
    data.extend_from_slice(&[ 0b0000_0101, 0x80 ]);
    Account { address: entry.block_pubkey, owner: id::PROGRAM_ID, data }
}

/// Returns an SPL Token account holding `amount` of `mint`, owned by `owner`, in `state` (1 initialized, 2 frozen)
fn token_account(mint: &Address, owner: &Address, amount: u64, state: u8) -> Account {
    let mut data = vec![ 0u8; TOKEN_ACCOUNT_LEN ];
    data[0..32].copy_from_slice(mint.as_ref());
    data[32..64].copy_from_slice(owner.as_ref());
    data[64..72].copy_from_slice(&amount.to_le_bytes());
    data[108] = state;
    Account { address: Address::new_from_array([ 9; 32 ]), owner: id::SPL_TOKEN_PROGRAM, data }
}


/// Verifies an Immortal all three ways, checking that they agree, and returns the result
fn verify_all_ways(entry: &Account, token: &Account, wallet: &Address) -> Result<Entry, Error> {
    let raw = load::verify_immortal(&entry.owner, &entry.data, &token.owner, &token.data, wallet).map(|e| *e);
    let info = with_account_info(entry, |entry_info| with_account_info(token, |token_info| {
        account_info::verify_immortal(entry_info, token_info, wallet).map(|e| *e)
    }));
    let view = with_account_view(entry, |entry_view| with_account_view(token, |token_view| {
        account_view::verify_immortal(entry_view, token_view, wallet).map(|e| *e)
    }));
    assert_eq!(raw, info, "load and account_info disagree");
    assert_eq!(raw, view, "load and account_view disagree");
    raw
}

/// Loads an account as a T all three ways, checking that they agree, and returns the result
fn load_all_ways<T: load::AccountType + PartialEq + std::fmt::Debug>(account: &Account) -> Result<T, Error> {
    let raw = load::load::<T>(&account.owner, &account.data).map(|x| *x);
    let info = with_account_info(account, |info| account_info::load::<T>(info).map(|x| *x));
    let view = with_account_view(account, |view| account_view::load::<T>(view).map(|x| *x));
    assert_eq!(raw, info, "load and account_info disagree");
    assert_eq!(raw, view, "load and account_view disagree");
    raw
}

/// Checks a block against an entry all three ways, checking that they agree, and returns the result
fn check_block_all_ways(entry: &Entry, block: &Account) -> Result<Block, Error> {
    let raw = load::check_block_of_entry(entry, &block.address, &block.owner, &block.data).map(|b| *b);
    let info = with_account_info(block, |info| account_info::check_block_of_entry(entry, info).map(|b| *b));
    let view = with_account_view(block, |view| account_view::check_block_of_entry(entry, view).map(|b| *b));
    assert_eq!(raw, info, "load and account_info disagree");
    assert_eq!(raw, view, "load and account_view disagree");
    raw
}


/// Returns a revealed, owned entry with distinctive metadata
fn owned_entry(rng: &mut Rng) -> Entry {
    let mut entry = make_entry(rng.u32(), rng.u32(), rng.u16(), None);
    entry.purchase_price_lamports = 1 + (rng.u64() >> 8);
    entry.level = 3;
    entry.metadata.random[0] = rng.u32();
    entry.metadata.level_metadata[3].skill = 0xa7;
    entry
}


#[test]
fn genuine_immortals_are_verified() {
    let mut rng = Rng::new("genuine_immortals_are_verified");
    let wallet = rng.address();
    let entry = owned_entry(&mut rng);
    let entry_account = entry_account(&entry);

    // Held in an initialized token account
    let token = token_account(&entry.mint_pubkey, &wallet, 1, 1);
    assert_eq!(verify_all_ways(&entry_account, &token, &wallet), Ok(entry));

    // Held in a frozen token account, as a staked Immortal is
    let mut staked = entry;
    staked.owned.stake_account = rng.address();
    let frozen = token_account(&entry.mint_pubkey, &wallet, 1, 2);
    assert_eq!(verify_all_ways(&self::entry_account(&staked), &frozen, &wallet), Ok(staked));
    assert!(load::read_token_account(&frozen.owner, &frozen.data).unwrap().is_frozen);
}


#[test]
fn forged_and_unsuitable_immortals_are_rejected() {
    let mut rng = Rng::new("forged_and_unsuitable_immortals_are_rejected");
    let wallet = rng.address();
    let entry = owned_entry(&mut rng);
    let genuine_entry = entry_account(&entry);
    let genuine_token = token_account(&entry.mint_pubkey, &wallet, 1, 1);

    // An entry with the same bytes, in an account that another program owns: the most important rejection
    let mut forged = genuine_entry.clone();
    forged.owner = rng.address();
    assert_eq!(verify_all_ways(&forged, &genuine_token, &wallet), Err(Error::WrongOwner));

    // An account of the program that is not an entry (a block's data type)
    let mut not_entry = genuine_entry.clone();
    not_entry.data[0] = 2;
    assert_eq!(verify_all_ways(&not_entry, &genuine_token, &wallet), Err(Error::WrongDataType));

    // Too short to be an entry
    let mut short = genuine_entry.clone();
    short.data.truncate(std::mem::size_of::<Entry>() - 1);
    assert_eq!(verify_all_ways(&short, &genuine_token, &wallet), Err(Error::DataTooShort));

    // Reauthorized to another program
    let mut reauthorized = entry;
    reauthorized.reauthorized = 1;
    assert_eq!(verify_all_ways(&entry_account(&reauthorized), &genuine_token, &wallet), Err(Error::Reauthorized));

    // A token account that the SPL Token program does not own (any other program could make one with these bytes)
    let mut forged_token = genuine_token.clone();
    forged_token.owner = rng.address();
    assert_eq!(verify_all_ways(&genuine_entry, &forged_token, &wallet), Err(Error::WrongOwner));

    // A token account of another mint
    let other_mint = token_account(&rng.address(), &wallet, 1, 1);
    assert_eq!(verify_all_ways(&genuine_entry, &other_mint, &wallet), Err(Error::WrongMint));

    // A token account of another wallet
    let other_owner = token_account(&entry.mint_pubkey, &rng.address(), 1, 1);
    assert_eq!(verify_all_ways(&genuine_entry, &other_owner, &wallet), Err(Error::WrongTokenOwner));

    // A token account that no longer holds the token
    let empty = token_account(&entry.mint_pubkey, &wallet, 0, 1);
    assert_eq!(verify_all_ways(&genuine_entry, &empty, &wallet), Err(Error::TokenNotHeld));

    // An uninitialized token account, and SPL Token accounts that are not token accounts (a mint is 82 bytes)
    let uninitialized = token_account(&entry.mint_pubkey, &wallet, 1, 0);
    assert_eq!(verify_all_ways(&genuine_entry, &uninitialized, &wallet), Err(Error::NotTokenAccount));
    let mut mint_sized = genuine_token.clone();
    mint_sized.data.truncate(82);
    assert_eq!(verify_all_ways(&genuine_entry, &mint_sized, &wallet), Err(Error::NotTokenAccount));
}


#[test]
fn accounts_are_loaded() {
    let mut rng = Rng::new("accounts_are_loaded");
    let entry = owned_entry(&mut rng);

    // Each account type loads only from its own data type
    let mut state: ProgramState = bytemuck::Zeroable::zeroed();
    state.data_type = 1;
    state.admin_pubkey = rng.address();
    let state_account = Account { address: id::PROGRAM_STATE, owner: id::PROGRAM_ID,
                                  data: bytemuck::bytes_of(&state).to_vec() };
    assert_eq!(load_all_ways::<ProgramState>(&state_account), Ok(state));
    assert_eq!(load_all_ways::<Entry>(&state_account), Err(Error::DataTooShort));
    let mut padded = state_account.clone();
    padded.data.resize(std::mem::size_of::<Entry>(), 0);
    assert_eq!(load_all_ways::<Entry>(&padded), Err(Error::WrongDataType));

    // A block loads with its bitmap after it, which Block::entry_added reads
    let block = block_account(&entry);
    let loaded = load_all_ways::<Block>(&block).unwrap();
    assert_eq!(loaded.block_start_timestamp, 1_700_000_000);
    let added: Vec<u16> = (0..16).filter(|&i| Block::entry_added(&block.data, i)).collect();
    assert_eq!(added, vec![ 0, 2, 15 ]);
    assert!(!Block::entry_added(&block.data, 1000), "an index beyond the bitmap is not added");

    // Data that is not aligned cannot be read in place, but can be copied
    let buffer = aligned(&block.data, 1);
    let misaligned = &bytemuck::cast_slice::<u64, u8>(&buffer)[1..(1 + block.data.len())];
    assert_eq!(load::from_bytes::<Block>(misaligned).err(), Some(Error::Misaligned));
    assert_eq!(load::read::<Block>(misaligned), Ok(loaded));

    // The errors convert to ProgramErrors
    assert_eq!(ProgramError::from(Error::WrongOwner), ProgramError::IllegalOwner);
    assert_eq!(ProgramError::from(Error::DataTooShort), ProgramError::AccountDataTooSmall);
    assert_eq!(ProgramError::from(Error::WrongMint), ProgramError::InvalidAccountData);
}


#[test]
fn blocks_of_entries_are_checked() {
    let mut rng = Rng::new("blocks_of_entries_are_checked");
    let entry = owned_entry(&mut rng);
    let block = block_account(&entry);
    assert!(check_block_all_ways(&entry, &block).is_ok());

    // Another block
    let mut other = block.clone();
    other.address = rng.address();
    assert_eq!(check_block_all_ways(&entry, &other), Err(Error::WrongBlock));

    // The entry's block address, but an account that another program owns
    let mut forged = block.clone();
    forged.owner = rng.address();
    assert_eq!(check_block_all_ways(&entry, &forged), Err(Error::WrongOwner));
}


#[test]
fn entry_methods() {
    let mut rng = Rng::new("entry_methods");
    let mut entry = owned_entry(&mut rng);

    // A revealed entry's metadata is available, at its current level
    assert!(entry.is_revealed());
    assert_eq!(entry.revealed_metadata().unwrap().random[0], entry.metadata.random[0]);
    let level = entry.current_level_metadata().unwrap();
    assert_eq!((level.attack(), level.defense()), (0xa, 0x7));

    // An unrevealed entry's metadata is not final, and so not available
    entry.reveal_sha256 = rng.bytes32();
    assert!(!entry.is_revealed());
    assert!(entry.revealed_metadata().is_none());
    assert!(entry.current_level_metadata().is_none());
    entry.reveal_sha256 = [ 0; 32 ];

    // An invalid level has no metadata, rather than panicking
    entry.level = 9;
    assert!(entry.current_level_metadata().is_none());
    entry.level = 3;

    // Strings stop at their first NUL, or fill the field
    let mut level = entry.metadata.level_metadata[0];
    level.name[..6].copy_from_slice(b"Kage\0x");
    assert_eq!(level.name(), Some("Kage"));
    level.title = [ b't'; 32 ];
    assert_eq!(level.title(), Some("t".repeat(32).as_str()));
    level.uri[0] = 0xff;
    assert_eq!(level.uri(), None, "invalid UTF-8");

    // epochs_staked is always current for an entry that is not staked; for a staked one, once it has been counted to
    // the current epoch
    entry.epochs_staked_counted_to_epoch = 100;
    assert!(entry.epochs_staked_is_current(500));
    entry.owned.stake_account = rng.address();
    assert!(entry.is_staked());
    assert!(entry.epochs_staked_is_current(100));
    assert!(!entry.epochs_staked_is_current(101));
}


#[test]
fn entry_states() {
    let mut rng = Rng::new("entry_states");
    let now = 1_800_000_000;
    let base = make_entry(rng.u32(), rng.u32(), rng.u16(), None);
    let mut block: Block = bytemuck::Zeroable::zeroed();
    block.config.total_mystery_count = 10;
    block.block_start_timestamp = now - 1000;
    block.config.mystery_phase_duration = 5000;

    // Unrevealed: the specific states need the block
    let mut entry = base;
    entry.reveal_sha256 = [ 1; 32 ];
    assert_eq!(entry.state(None, now), EntryState::PreReveal);
    assert_eq!(entry.state(Some(&block), now), EntryState::PreRevealUnowned);
    entry.purchase_price_lamports = 5;
    assert_eq!(entry.state(Some(&block), now), EntryState::PreRevealOwned);
    // Once every mystery is sold, the block is revealable
    block.mysteries_sold_count = 10;
    assert_eq!(entry.state(Some(&block), now), EntryState::WaitingForRevealOwned);
    entry.purchase_price_lamports = 0;
    assert_eq!(entry.state(Some(&block), now), EntryState::WaitingForRevealUnowned);
    // Or once the mystery phase is over
    block.mysteries_sold_count = 3;
    assert_eq!(entry.state(Some(&block), now + 4001), EntryState::WaitingForRevealUnowned);
    assert_eq!(entry.state(Some(&block), now + 4000), EntryState::PreRevealUnowned);

    // Revealed
    let mut entry = base;
    entry.reveal_timestamp = now - 100;
    entry.duration = 200;
    assert_eq!(entry.state(None, now), EntryState::Unowned);
    entry.has_auction = 1;
    assert_eq!(entry.state(None, now), EntryState::InAuction);
    assert_eq!(entry.state(None, now + 100), EntryState::Unowned, "auction over without bids");
    entry.auction.highest_bid_lamports = 7;
    assert_eq!(entry.state(None, now + 100), EntryState::WaitingToBeClaimed);
    entry.purchase_price_lamports = 7;
    assert_eq!(entry.state(None, now + 100), EntryState::Owned);
    entry.owned.stake_account = rng.address();
    assert_eq!(entry.state(None, now + 100), EntryState::OwnedAndStaked);

    // Reauthorized overrides everything
    entry.reauthorized = 1;
    assert_eq!(entry.state(Some(&block), now), EntryState::Reauthorized);
}


#[test]
fn whitelist_entries() {
    let mut rng = Rng::new("whitelist_entries");
    let mut whitelist: Whitelist = bytemuck::Zeroable::zeroed();
    whitelist.data_type = 5;
    for i in 0..3 {
        whitelist.entries[i] = rng.address();
    }
    whitelist.count = 3;
    assert_eq!(whitelist.entries(), &whitelist.entries[..3]);
    // A count beyond the capacity is clamped
    whitelist.count = u16::MAX;
    assert_eq!(whitelist.entries().len(), MAX_WHITELIST_ENTRIES);
}
