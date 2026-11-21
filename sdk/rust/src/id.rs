//! The address of the Shinobi Immortals program, and the fixed addresses that it uses.
//!
//! These are the addresses that `build_program.sh` compiles into the program; the tests check them against it.  The
//! program's own accounts at fixed addresses (its program state, authority, and Ki and bid marker mints) are program
//! derived addresses of single-byte seeds, whose bump seeds are given here too, so that on chain they can be checked
//! with `Address::derive_address` rather than the much more expensive `find_program_address`.

use solana_address::{address, Address};

/// The Shinobi Immortals program
pub const PROGRAM_ID: Address = address!("Shin1cdrR1pmemXZU3yDV3PnQ48SX9UmrtHF4GbKzWG");

/// The program state account, which holds the address of the admin, and the reward rate that LevelUp uses
/// ([`crate::ProgramState`]).  Seed: `[1]`.
pub const PROGRAM_STATE: Address = address!("2kq3azddKFDmNqa9HGjzwm2K4zf97B837SyTQo3cqRbN");

/// The bump seed of [`PROGRAM_STATE`]
pub const PROGRAM_STATE_BUMP_SEED: u8 = 253;

/// The program's authority account ([`crate::Authority`]): the mint authority, freeze authority, metadata update
/// authority, and stake authority of everything that the program controls.  Seed: `[2]`.
pub const AUTHORITY: Address = address!("Da1fxePFBL9bU7VoZ4SNEnQXgVr9aYL8wjDj6Chjayvz");

/// The bump seed of [`AUTHORITY`]
pub const AUTHORITY_BUMP_SEED: u8 = 253;

/// The mint of the Ki token, which staked Immortals earn.  Seed: `[3]`.
pub const KI_MINT: Address = address!("7WZgQck1WqvmR5CDuLkdG91J56xDMwqnLXkAuZyJJzgS");

/// The bump seed of [`KI_MINT`]
pub const KI_MINT_BUMP_SEED: u8 = 254;

/// The Metaplex metadata of the Ki mint
pub const KI_METADATA: Address = address!("seVW89riWk2hK1VZLauqZjo6Rm46F6ydCzL5q8bnyGF");

/// The mint of bid marker tokens, which mark the bids made on auctioned entries.  Seed: `[7]`.
pub const BID_MARKER_MINT: Address = address!("DGZHfBmb8GSL4cg4cSxEnC8zVcQt8br9TSB8NrbEfPa1");

/// The bump seed of [`BID_MARKER_MINT`]
pub const BID_MARKER_MINT_BUMP_SEED: u8 = 253;

/// The Metaplex metadata of the bid marker mint
pub const BID_MARKER_METADATA: Address = address!("93mNAwtPeYf7QwDodz4WGdxoWfbegHuNiRh3R9bDMUw5");

/// The reference stake account: a stake account, delegated to Shinobi Systems and controlled only by the program,
/// from which the program's MeasureRewardRate instruction measures the reward rate that a delegator to Shinobi Systems
/// earns.  Seed: `[12]`.
pub const REFERENCE_STAKE: Address = address!("EmoRfMeoBb5WdEjjNjxavp6heWNPgZxMYZ5BRpHc6bjv");

/// The bump seed of [`REFERENCE_STAKE`]
pub const REFERENCE_STAKE_BUMP_SEED: u8 = 255;

/// The vote account of the Shinobi Systems validator, the only validator that an Immortal's stake account may be
/// delegated to
pub const SHINOBI_SYSTEMS_VOTE: Address = address!("BLADE1qNA1uNjRgER6DtUFf7FU3c1TWLLdpPeEcKatZ2");

/// The system program
pub const SYSTEM_PROGRAM: Address = address!("11111111111111111111111111111111");

/// The SPL Token program, which holds Immortals' tokens and Ki.  (Not SPL Token 2022: the program uses only the
/// immutable SPL Token program.)
pub const SPL_TOKEN_PROGRAM: Address = address!("TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA");

/// The SPL Associated Token Account program
pub const SPL_ASSOCIATED_TOKEN_PROGRAM: Address = address!("ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL");

/// The Metaplex Token Metadata program
pub const METAPLEX_PROGRAM: Address = address!("metaqbxxUerdq28cj1RbAWkYQm3ybzjb6a8bt518x1s");

/// The stake program
pub const STAKE_PROGRAM: Address = address!("Stake11111111111111111111111111111111111111");

/// The stake config account, which the stake program requires for delegation
pub const STAKE_CONFIG: Address = address!("StakeConfig11111111111111111111111111111111");

/// The clock sysvar
pub const CLOCK_SYSVAR: Address = address!("SysvarC1ock11111111111111111111111111111111");

/// The rent sysvar
pub const RENT_SYSVAR: Address = address!("SysvarRent111111111111111111111111111111111");

/// The stake history sysvar
pub const STAKE_HISTORY_SYSVAR: Address = address!("SysvarStakeHistory1111111111111111111111111");

/// The epoch rewards sysvar
pub const EPOCH_REWARDS_SYSVAR: Address = address!("SysvarEpochRewards1111111111111111111111111");
