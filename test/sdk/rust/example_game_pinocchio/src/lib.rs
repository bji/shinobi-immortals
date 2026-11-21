//! An example of a game program that uses Shinobi Immortals as game pieces, built with Pinocchio.
//!
//! This does exactly what the example built with the standard Solana crates (../example_game) does, with Pinocchio's
//! `AccountView` in place of `AccountInfo`; see there for what it does and why.
//!
//! Accounts: 0, the player (signer); 1, the Immortal's entry; 2, the token account holding it; 3, the entry's block.

use pinocchio::{error::ProgramError, AccountView, Address, ProgramResult};
use shinobi_immortals::account_view::{check_block_of_entry, verify_immortal};

pinocchio::entrypoint!(process_instruction);

/// Reports the player's Immortal
pub fn process_instruction(_program_id: &Address, accounts: &mut [AccountView], _data: &[u8]) -> ProgramResult {
    let [ player, entry, token, block ] = accounts else {
        return Err(ProgramError::NotEnoughAccountKeys);
    };

    // 1. The player must have signed
    if !player.is_signer() {
        return Err(ProgramError::MissingRequiredSignature);
    }

    // 2. The Immortal must be genuine, current, and the player's
    let entry = verify_immortal(entry, token, player.address())?;

    // 3. The block must be the entry's
    let block = check_block_of_entry(&entry, block)?;

    // 4. Only a revealed Immortal's metadata is final
    let level = entry.current_level_metadata().ok_or(ProgramError::InvalidAccountData)?;

    // The report, as the other example makes it
    let mut report = [ 0u8; 27 ];
    report[0] = entry.level;
    report[1] = level.attack();
    report[2] = level.defense();
    report[3..7].copy_from_slice(&level.form.to_le_bytes());
    report[7..11].copy_from_slice(&entry.metadata.random[0].to_le_bytes());
    report[11..19].copy_from_slice(&entry.epochs_staked.to_le_bytes());
    report[19..27].copy_from_slice(&block.block_start_timestamp.to_le_bytes());
    set_return_data(&report);

    Ok(())
}

/// Sets the instruction's return data (a syscall, available only on chain)
fn set_return_data(data: &[u8]) {
    #[cfg(any(target_os = "solana", target_arch = "bpf"))]
    unsafe {
        pinocchio::syscalls::sol_set_return_data(data.as_ptr(), data.len() as u64);
    }
    #[cfg(not(any(target_os = "solana", target_arch = "bpf")))]
    let _ = data;
}
