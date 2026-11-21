//! Checked loading of the program's accounts, and verification of Immortals, from raw account parts.
//!
//! The functions here take the parts of an account (its owner and its data, and where needed its address) rather than
//! any framework's account type, so that they work with any framework.  [`crate::account_info`] and
//! [`crate::account_view`] apply them to the standard Solana crates' `AccountInfo` and to Pinocchio's `AccountView`.
//!
//! # Why an account can be trusted
//!
//! Only the program that owns an account can change its data.  So an account that is owned by the Shinobi Immortals
//! program, and whose data type (its first field) is that of an entry, was written as an entry by the program itself,
//! and only the program creates entries, each at its own program derived address.  That is all that is needed to
//! trust an entry: checking its address as well would cost a program derived address derivation, and add nothing.
//! [`load`] checks exactly the owner and the data type (and that the data is long enough).
//!
//! Decoding an account's data without checking its owner ([`from_bytes`], [`read`]) is only safe for data that is
//! already trusted, such as data that a client has fetched from a cluster by address.

use crate::id;
use crate::state::{Authority, Bid, Block, DataType, Entry, ProgramState, Whitelist};
use bytemuck::Pod;
use solana_address::Address;

/// The ways in which loading or verification can fail
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Error {
    /// The account is not owned by the program that it must be owned by (the Shinobi Immortals program, or for a
    /// token account, the SPL Token program)
    WrongOwner,
    /// The account's data is too short to hold the account type
    DataTooShort,
    /// The account's data is not of the expected type: its `data_type` is not that of the account type
    WrongDataType,
    /// The account's data is not aligned for the account type, so it cannot be read in place (use [`read`] instead)
    Misaligned,
    /// The entry has been reauthorized to another program, and is no longer managed by this one
    Reauthorized,
    /// The block account is not the entry's block
    WrongBlock,
    /// The token account is not an initialized SPL Token account
    NotTokenAccount,
    /// The token account is not for the entry's mint
    WrongMint,
    /// The token account is not owned by the expected wallet
    WrongTokenOwner,
    /// The token account does not hold the entry's token
    TokenNotHeld,
    /// The account's data could not be borrowed (it is already borrowed mutably)
    AccountBorrowFailed,
}

impl core::fmt::Display for Error {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.write_str(match self {
            Error::WrongOwner => "the account is not owned by the expected program",
            Error::DataTooShort => "the account's data is too short for its type",
            Error::WrongDataType => "the account's data is not of the expected type",
            Error::Misaligned => "the account's data is not aligned for its type",
            Error::Reauthorized => "the entry has been reauthorized to another program",
            Error::WrongBlock => "the block is not the entry's block",
            Error::NotTokenAccount => "the account is not an initialized SPL Token account",
            Error::WrongMint => "the token account is not for the entry's mint",
            Error::WrongTokenOwner => "the token account is not owned by the expected wallet",
            Error::TokenNotHeld => "the token account does not hold the entry's token",
            Error::AccountBorrowFailed => "the account's data could not be borrowed",
        })
    }
}

impl core::error::Error for Error {}

/// Errors convert to the `ProgramError` that best describes them, so that `?` works in a program
#[cfg(any(feature = "solana", feature = "pinocchio"))]
impl From<Error> for solana_program_error::ProgramError {
    fn from(error: Error) -> Self {
        use solana_program_error::ProgramError;
        match error {
            Error::WrongOwner => ProgramError::IllegalOwner,
            Error::DataTooShort => ProgramError::AccountDataTooSmall,
            Error::AccountBorrowFailed => ProgramError::AccountBorrowFailed,
            _ => ProgramError::InvalidAccountData,
        }
    }
}


/// The account types of the program, each with its data type.  Implemented only by this crate's account types.
pub trait AccountType: Pod + sealed::Sealed {
    /// The data type that the account's `data_type` must hold
    const DATA_TYPE: DataType;
}

mod sealed {
    pub trait Sealed {}
}

macro_rules! account_type {
    ($type:ty, $data_type:expr) => {
        impl sealed::Sealed for $type {}
        impl AccountType for $type {
            const DATA_TYPE: DataType = $data_type;
        }
    };
}

account_type!(ProgramState, DataType::ProgramState);
account_type!(Authority, DataType::Authority);
account_type!(Block, DataType::Block);
account_type!(Entry, DataType::Entry);
account_type!(Bid, DataType::Bid);
account_type!(Whitelist, DataType::Whitelist);


/// Checks that `data` is long enough for `T` and holds `T`'s data type
fn check_data<T: AccountType>(data: &[u8]) -> Result<(), Error> {
    if data.len() < core::mem::size_of::<T>() {
        return Err(Error::DataTooShort);
    }
    // Every account type begins with its 4 byte little-endian data type
    let data_type = u32::from_le_bytes([data[0], data[1], data[2], data[3]]);
    if data_type != (T::DATA_TYPE as u32) {
        return Err(Error::WrongDataType);
    }
    Ok(())
}


/// Returns the account data `data` as a `T`, in place, after checking that it is long enough and holds `T`'s data
/// type.  Any data after the structure (such as a block's `entries_added_bitmap`) is ignored.  This does NOT check
/// the account's owner (see [`load`]).
///
/// The data must be aligned for `T`, as account data passed to a program always is.  Data elsewhere (such as in a
/// `Vec<u8>`) may not be; [`read`] copies it instead.
pub fn from_bytes<T: AccountType>(data: &[u8]) -> Result<&T, Error> {
    check_data::<T>(data)?;
    bytemuck::try_from_bytes(&data[..core::mem::size_of::<T>()]).map_err(|_| Error::Misaligned)
}


/// Returns a copy of the account data `data` as a `T`, after checking that it is long enough and holds `T`'s data
/// type.  Unlike [`from_bytes`], the data may have any alignment.  This does NOT check the account's owner (see
/// [`load`]).
pub fn read<T: AccountType>(data: &[u8]) -> Result<T, Error> {
    check_data::<T>(data)?;
    Ok(bytemuck::pod_read_unaligned(&data[..core::mem::size_of::<T>()]))
}


/// Returns the data of an account as a `T`, in place, after checking that the account is owned by the Shinobi
/// Immortals program (`owner` is the account's owner) and that its data is a `T`.  This is what makes an account's
/// contents trustworthy (see the [module documentation](self)).
pub fn load<'a, T: AccountType>(owner: &Address, data: &'a [u8]) -> Result<&'a T, Error> {
    if *owner != id::PROGRAM_ID {
        return Err(Error::WrongOwner);
    }
    from_bytes(data)
}


// Token accounts -----------------------------------------------------------------------------------------------------

/// The size of an SPL Token account's data
pub const TOKEN_ACCOUNT_LEN: usize = 165;

/// The fields of an SPL Token account that verification needs
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct TokenAccount {
    /// The mint of the tokens that the account holds
    pub mint: Address,
    /// The wallet that owns the account
    pub owner: Address,
    /// The number of tokens held
    pub amount: u64,
    /// Whether the account is frozen (as a staked Immortal's token account is)
    pub is_frozen: bool,
}

/// Reads the fields of an SPL Token account, after checking that it is owned by the SPL Token program (`owner` is the
/// account's owner) and is an initialized token account.  The layout is that of `spl_token::state::Account`: the mint
/// (bytes 0 to 31), the owner (32 to 63), the amount (64 to 71, little-endian), a delegate (72 to 107), and the state
/// (byte 108: 0 uninitialized, 1 initialized, 2 frozen), in 165 bytes in all.
pub fn read_token_account(owner: &Address, data: &[u8]) -> Result<TokenAccount, Error> {
    if *owner != id::SPL_TOKEN_PROGRAM {
        return Err(Error::WrongOwner);
    }
    // A token account is exactly this size; a mint (82 bytes) or a multisig (355 bytes) is not
    if data.len() != TOKEN_ACCOUNT_LEN {
        return Err(Error::NotTokenAccount);
    }
    let state = data[108];
    if (state != 1) && (state != 2) {
        return Err(Error::NotTokenAccount);
    }
    let address_at = |offset: usize| {
        let mut bytes = [0u8; 32];
        bytes.copy_from_slice(&data[offset..(offset + 32)]);
        Address::new_from_array(bytes)
    };
    let mut amount = [0u8; 8];
    amount.copy_from_slice(&data[64..72]);
    Ok(TokenAccount {
        mint: address_at(0),
        owner: address_at(32),
        amount: u64::from_le_bytes(amount),
        is_frozen: state == 2,
    })
}


// Verification -------------------------------------------------------------------------------------------------------

/// Verifies that an entry account is a genuine Shinobi Immortal, which has not been reauthorized to another program,
/// and that `wallet` owns it, and returns the entry.
///
/// - `entry_owner` and `entry_data` are the owner and data of the entry account
/// - `token_owner` and `token_data` are the owner and data of the token account that holds the Immortal's token
/// - `wallet` is the wallet that must own the Immortal
///
/// The entry is checked as [`load`] checks it.  The token account must be an SPL Token account for the entry's mint,
/// owned by `wallet`, holding the token.  Each Immortal's mint has exactly one token, ever, so the account that holds
/// it is the only one that can.  A staked Immortal's token account is frozen, and is accepted.
///
/// This does not check that `wallet` signed the transaction; a program that needs that must check it itself.
pub fn verify_immortal<'a>(entry_owner: &Address, entry_data: &'a [u8], token_owner: &Address, token_data: &[u8],
                           wallet: &Address) -> Result<&'a Entry, Error> {
    let entry = load::<Entry>(entry_owner, entry_data)?;
    check_immortal(entry, token_owner, token_data, wallet)?;
    Ok(entry)
}


/// The checks of [`verify_immortal`] after the entry is loaded, so that each framework's version can load the entry in
/// its own way
pub(crate) fn check_immortal(entry: &Entry, token_owner: &Address, token_data: &[u8], wallet: &Address)
                             -> Result<(), Error> {
    if entry.reauthorized() {
        return Err(Error::Reauthorized);
    }
    let token = read_token_account(token_owner, token_data)?;
    if token.mint != entry.mint_pubkey {
        return Err(Error::WrongMint);
    }
    if token.owner != *wallet {
        return Err(Error::WrongTokenOwner);
    }
    if token.amount != 1 {
        return Err(Error::TokenNotHeld);
    }
    Ok(())
}


/// Verifies that a block account is the block of `entry` (which must itself be trusted, as from [`load`] or
/// [`verify_immortal`]), and returns the block.  `block_address`, `block_owner`, and `block_data` are the block
/// account's address, owner, and data.
pub fn check_block_of_entry<'a>(entry: &Entry, block_address: &Address, block_owner: &Address, block_data: &'a [u8])
                                -> Result<&'a Block, Error> {
    if *block_address != entry.block_pubkey {
        return Err(Error::WrongBlock);
    }
    load::<Block>(block_owner, block_data)
}
