//! An example of a game program that uses Shinobi Immortals as game pieces, built with the standard Solana crates.
//!
//! Its one instruction takes a player's Immortal and reports what a game would use of it.  It shows everything that a
//! game must do before trusting an Immortal:
//!
//! 1. Check that the player signed
//! 2. Verify the Immortal with `verify_immortal`: that the entry account is the Shinobi Immortals program's, that it
//!    has not been reauthorized to another program, and that the token account (an SPL Token account) holds the
//!    Immortal's token for the player
//! 3. Verify the entry's block with `check_block_of_entry`, if the block is needed (here, for the Immortal's age)
//! 4. Use only revealed metadata (`current_level_metadata`), which is final
//!
//! Accounts: 0, the player (signer); 1, the Immortal's entry; 2, the token account holding it; 3, the entry's block.
//!
//! The report is the instruction's return data, as little-endian values: the level (u8), attack (u8), defense (u8),
//! form (u32), the first random number (u32), epochs staked (u64), and the time at which the Immortal's life began
//! (i64).

use shinobi_immortals::account_info::{check_block_of_entry, verify_immortal};
use solana_account_info::AccountInfo;
use solana_address::Address;
use solana_program_error::{ProgramError, ProgramResult};

solana_program_entrypoint::entrypoint!(process_instruction);

/// Reports the player's Immortal
pub fn process_instruction(_program_id: &Address, accounts: &[AccountInfo], _data: &[u8]) -> ProgramResult {
    let [ player, entry, token, block ] = accounts else {
        return Err(ProgramError::NotEnoughAccountKeys);
    };

    // 1. The player must have signed
    if !player.is_signer {
        return Err(ProgramError::MissingRequiredSignature);
    }

    // 2. The Immortal must be genuine, current, and the player's
    let entry = verify_immortal(entry, token, player.key)?;

    // 3. The block must be the entry's
    let block = check_block_of_entry(&entry, block)?;

    // 4. Only a revealed Immortal's metadata is final
    let level = entry.current_level_metadata().ok_or(ProgramError::InvalidAccountData)?;

    let mut report = Vec::with_capacity(27);
    report.push(entry.level);
    report.push(level.attack());
    report.push(level.defense());
    report.extend_from_slice(&level.form.to_le_bytes());
    report.extend_from_slice(&entry.metadata.random[0].to_le_bytes());
    report.extend_from_slice(&entry.epochs_staked.to_le_bytes());
    report.extend_from_slice(&block.block_start_timestamp.to_le_bytes());
    solana_cpi::set_return_data(&report);

    Ok(())
}
