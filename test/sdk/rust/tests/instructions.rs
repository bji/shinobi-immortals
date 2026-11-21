// Tests every instruction builder of the Rust crate against the transaction that the corresponding script in scripts/
// makes for the same inputs.  The scripts are the reference for each instruction's accounts and data, and are what
// the program tests (test/program) use to exercise the program.
//
// Each script's transaction is decoded with "solxact decode", and its instruction compared with the builder's: the
// program, every account in order, whether each signs and is writable, and the data.  A transaction gives each address
// one set of permissions (the most that any use of it needs; and the fee payer always signs and is writable), so the
// builder's accounts are compared after combining their permissions in the same way.

mod common;

use common::{make_entry, run_script, solxact_decode, Rng};
use shinobi_immortals::instruction::{self, BuildError, WithdrawExcessAccount};
use shinobi_immortals::{pda, Address, BlockConfiguration};
use solana_instruction::Instruction;
use std::collections::{BTreeSet, HashMap};

/// The account list of an instruction: each account's address, and whether it signs and is writable
type Accounts = Vec<(String, bool, bool)>;

/// Compares instructions made by the builders with those of the scripts, collecting every difference
struct Comparison {
    failures: Vec<String>,
    codes: BTreeSet<u8>,
}

impl Comparison {
    /// Compares `instruction` with the instruction of the transaction that `script` makes with `args`
    fn compare(&mut self, description: &str, script: &str, args: &[String], instruction: Instruction) {
        self.codes.insert(instruction.data[0]);

        let decoded = solxact_decode(&run_script(script, args));
        let instructions = decoded["instructions"].as_array().unwrap();
        if instructions.len() != 1 {
            self.failures.push(format!("{}: the script makes {} instructions", description, instructions.len()));
            return;
        }
        let expected = &instructions[0];
        let expected_accounts: Accounts = expected["addresses"].as_array().unwrap().iter()
            .map(|a| (a["address"].as_str().unwrap().to_string(), a["is_signed"].as_bool().unwrap_or(false),
                      a["is_read_write"].as_bool().unwrap_or(false)))
            .collect();
        let expected_data: Vec<u8> = expected["data"].as_array().unwrap().iter()
            .map(|b| b.as_u64().unwrap() as u8).collect();

        // Combine the permissions of the builder's accounts as a transaction does, including the fee payer's
        let fee_payer = decoded["addresses"].as_array().unwrap().iter()
            .find(|a| a["fee_payer"].as_bool() == Some(true)).unwrap()["address"].as_str().unwrap().to_string();
        let mut permissions: HashMap<String, (bool, bool)> = HashMap::new();
        permissions.insert(fee_payer, (true, true));
        for account in &instruction.accounts {
            let entry = permissions.entry(account.pubkey.to_string()).or_insert((false, false));
            entry.0 |= account.is_signer;
            entry.1 |= account.is_writable;
        }
        let actual_accounts: Accounts = instruction.accounts.iter()
            .map(|a| {
                let (is_signer, is_writable) = permissions[&a.pubkey.to_string()];
                (a.pubkey.to_string(), is_signer, is_writable)
            })
            .collect();

        if instruction.program_id.to_string() != expected["program_id"].as_str().unwrap() {
            self.failures.push(format!("{}: program {}", description, instruction.program_id));
        }
        if actual_accounts.len() != expected_accounts.len() {
            self.failures.push(format!("{}: {} accounts, the script has {}", description, actual_accounts.len(),
                                       expected_accounts.len()));
        }
        for (i, (actual, expected)) in actual_accounts.iter().zip(expected_accounts.iter()).enumerate() {
            if actual != expected {
                self.failures.push(format!("{}: account {}: builder {:?}, script {:?}", description, i, actual,
                                           expected));
            }
        }
        if instruction.data != expected_data {
            self.failures.push(format!("{}: data: builder {}, script {}", description,
                                       common::hex(&instruction.data), common::hex(&expected_data)));
        }
    }
}


/// Converts arguments to the strings that the scripts take
macro_rules! args {
    ($($arg:expr),* $(,)?) => { vec![ $($arg.to_string()),* ] };
}

/// Returns the base64 encoding of bytes (which the SetMetadataBytes script takes)
fn base64(bytes: &[u8]) -> String {
    const ALPHABET: &[u8] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut out = String::new();
    for chunk in bytes.chunks(3) {
        let n = ((chunk[0] as u32) << 16) | ((*chunk.get(1).unwrap_or(&0) as u32) << 8) |
            (*chunk.get(2).unwrap_or(&0) as u32);
        for i in 0..4 {
            if i <= chunk.len() {
                out.push(ALPHABET[((n >> (18 - (6 * i))) & 63) as usize] as char);
            }
            else {
                out.push('=');
            }
        }
    }
    out
}


#[test]
fn builders_match_the_scripts() {
    let mut rng = Rng::new("builders_match_the_scripts");
    let mut c = Comparison { failures: Vec::new(), codes: BTreeSet::new() };

    let superuser = rng.address();
    let admin = rng.address();
    let user = rng.address();
    let other = rng.address();
    let stake = rng.address();
    let token = rng.address();
    let g = rng.u32();
    let b = rng.u32();
    let i = rng.u16();
    let entry = make_entry(g, b, i, None);
    let staked_entry = make_entry(g, b, i, Some(&stake));
    let block = pda::block_address(g, b);

    // Superuser instructions

    c.compare("Initialize", "super_initialize_tx.sh", &args![superuser, admin],
              instruction::initialize(&superuser, &admin));
    c.compare("SetAdmin", "super_set_admin_tx.sh", &args![superuser, other],
              instruction::set_admin(&superuser, &other));

    // Admin instructions

    for (has_auction, whitelist_duration) in [ (true, 1 + rng.below(100000) as u32), (false, 0) ] {
        let mut config: BlockConfiguration = bytemuck::Zeroable::zeroed();
        config.group_number = g;
        config.block_number = b;
        config.total_entry_count = rng.u16();
        config.total_mystery_count = rng.u16();
        config.mystery_phase_duration = rng.u32();
        config.mystery_start_price_lamports = rng.u64();
        config.reveal_period_duration = rng.u32();
        config.minimum_price_lamports = rng.u64();
        config.has_auction = has_auction as u8;
        config.duration = rng.u32();
        config.final_start_price_lamports = rng.u64();
        config.whitelist_duration = whitelist_duration;
        config.expected_annual_reward_bps = rng.u32();
        c.compare(&format!("CreateBlock (has_auction {})", has_auction), "admin_create_block_tx.sh",
                  &args![admin, g, b, config.total_entry_count, config.total_mystery_count,
                         config.mystery_phase_duration, config.mystery_start_price_lamports,
                         config.reveal_period_duration, config.minimum_price_lamports, has_auction, config.duration,
                         config.final_start_price_lamports, whitelist_duration, config.expected_annual_reward_bps],
                  instruction::create_block(&admin, &config, None));
    }

    let sha256s: Vec<[u8; 32]> = (0..3).map(|_| rng.bytes32()).collect();
    let sha256_args: Vec<String> = sha256s.iter().map(|s| common::hex(s)).collect();
    let uri = "https://shinobi.example/initial.json";
    let mut script_args = args![admin, g, b, uri, "none", i];
    script_args.extend(sha256_args.iter().cloned());
    c.compare("AddEntriesToBlock", "admin_add_entries_to_block_tx.sh", &script_args,
              instruction::add_entries_to_block(&admin, g, b, uri, None, i, &sha256s, None).unwrap());
    c.compare("AddEntriesToBlock (second creator)", "admin_add_entries_to_block_tx.sh",
              &args![admin, g, b, "u", other, 0, sha256_args[0]],
              instruction::add_entries_to_block(&admin, g, b, "u", Some(&other), 0, &sha256s[..1], None).unwrap());
    let long_uri = "x".repeat(200);
    let mut script_args = args![admin, g, b, long_uri, "none", 7];
    script_args.extend(sha256_args[..2].iter().cloned());
    c.compare("AddEntriesToBlock (200 byte uri)", "admin_add_entries_to_block_tx.sh", &script_args,
              instruction::add_entries_to_block(&admin, g, b, &long_uri, None, 7, &sha256s[..2], None).unwrap());

    let metadata_length = 1 + rng.below(600) as usize;
    let metadata_bytes = rng.bytes(metadata_length);
    let start_index = rng.below(2000) as u16;
    c.compare("SetMetadataBytes", "admin_set_metadata_bytes_tx.sh",
              &args![admin, g, b, i, start_index, base64(&metadata_bytes)],
              instruction::set_metadata_bytes(&admin, g, b, i, start_index, &metadata_bytes).unwrap());

    let salts = [ rng.u64(), u64::MAX, 0 ];
    c.compare("RevealEntries", "admin_reveal_entries_tx.sh", &args![admin, g, b, i, salts[0], salts[1], salts[2]],
              instruction::reveal_entries(&admin, g, b, i, &salts).unwrap());

    for count in [ 1, 3, instruction::MAX_WHITELIST_ADD_ENTRIES ] {
        let addresses: Vec<Address> = (0..count).map(|_| rng.address()).collect();
        let mut script_args = args![admin, g, b];
        script_args.extend(addresses.iter().map(|a| a.to_string()));
        c.compare(&format!("AddWhitelistEntries ({})", count), "admin_add_whitelist_entries_tx.sh", &script_args,
                  instruction::add_whitelist_entries(&admin, g, b, &addresses, None).unwrap());
    }

    c.compare("DeleteWhitelist", "admin_delete_whitelist_tx.sh", &args![admin, g, b],
              instruction::delete_whitelist(&admin, g, b));

    c.compare("WithdrawExcess (program state)", "admin_withdraw_excess_tx.sh", &args![admin, "state"],
              instruction::withdraw_excess(&admin, &WithdrawExcessAccount::ProgramState));
    c.compare("WithdrawExcess (authority)", "admin_withdraw_excess_tx.sh", &args![admin, "authority"],
              instruction::withdraw_excess(&admin, &WithdrawExcessAccount::Authority));
    c.compare("WithdrawExcess (block)", "admin_withdraw_excess_tx.sh", &args![admin, "block", g, b],
              instruction::withdraw_excess(&admin, &WithdrawExcessAccount::Block(block)));
    c.compare("WithdrawExcess (entry)", "admin_withdraw_excess_tx.sh", &args![admin, "entry", g, b, i],
              instruction::withdraw_excess(&admin, &WithdrawExcessAccount::Entry(pda::entry_address(
                  &entry.mint_pubkey))));
    c.compare("WithdrawExcess (whitelist)", "admin_withdraw_excess_tx.sh", &args![admin, "whitelist", g, b],
              instruction::withdraw_excess(&admin, &WithdrawExcessAccount::Whitelist(block)));

    // User instructions

    let price = rng.u64();
    c.compare("Buy", "user_buy_tx.sh", &args![admin, user, g, b, i, price],
              instruction::buy(&entry, price, &user, &admin));

    c.compare("Refund", "user_refund_tx.sh", &args![user, g, b, i], instruction::refund(&entry, &user, None));
    c.compare("Refund (token account given)", "user_refund_tx.sh", &args![user, g, b, i, token],
              instruction::refund(&entry, &user, Some(&token)));

    let minimum_bid = rng.u64() >> 1;
    let maximum_bid = minimum_bid + (rng.u64() >> 2);
    c.compare("Bid", "user_bid_tx.sh", &args![user, g, b, i, minimum_bid, maximum_bid],
              instruction::bid(&entry, minimum_bid, maximum_bid, &user));

    // The script reclaims the bid marker only if asked to ("true"); the builder does unless asked not to
    c.compare("ClaimLosing (reclaiming the bid marker)", "user_claim_losing_tx.sh", &args![user, g, b, i, "true"],
              instruction::claim_losing(&entry, &user, false));
    c.compare("ClaimLosing (keeping the bid marker)", "user_claim_losing_tx.sh", &args![user, g, b, i],
              instruction::claim_losing(&entry, &user, true));

    c.compare("Stake", "user_stake_tx.sh", &args![user, g, b, i, stake], instruction::stake(&entry, &stake, &user));

    for (no_ki_harvest, no_thaw) in [ (false, false), (true, false), (false, true), (true, true) ] {
        let mut script_args = args![user, g, b, i, stake];
        if no_ki_harvest {
            script_args.push("no_ki_harvest".to_string());
        }
        if no_thaw {
            script_args.push("no_thaw".to_string());
        }
        c.compare(&format!("Destake (no_ki_harvest {}, no_thaw {})", no_ki_harvest, no_thaw), "user_destake_tx.sh",
                  &script_args, instruction::destake(&staked_entry, &user, no_ki_harvest, no_thaw));
    }

    c.compare("Thaw", "user_thaw_tx.sh", &args![user, g, b, i], instruction::thaw(&entry, &user));

    c.compare("Harvest", "user_harvest_tx.sh", &args![user, g, b, i, stake],
              instruction::harvest(&staked_entry, &user));

    let ki = rng.u64();
    c.compare("LevelUp (staked)", "user_level_up_tx.sh", &args![user, g, b, i, ki, stake],
              instruction::level_up(&staked_entry, ki, &user));
    c.compare("LevelUp (not staked)", "user_level_up_tx.sh", &args![user, g, b, i, ki, "none"],
              instruction::level_up(&entry, ki, &user));

    // Anyone instructions

    c.compare("SettleWinningBid (by another account)", "anyone_settle_winning_bid_tx.sh",
              &args![other, admin, user, g, b, i],
              instruction::settle_winning_bid(&entry, &user, &admin, &other, false));
    c.compare("SettleWinningBid (by the bidder)", "anyone_settle_winning_bid_tx.sh",
              &args![user, admin, user, g, b, i], instruction::settle_winning_bid(&entry, &user, &admin, &user, false));
    c.compare("SettleWinningBid (by the bidder, keeping the bid marker)", "anyone_settle_winning_bid_tx.sh",
              &args![user, admin, user, g, b, i, "keep_bid_marker"],
              instruction::settle_winning_bid(&entry, &user, &admin, &user, true));

    c.compare("Delegate", "anyone_delegate_tx.sh", &args![other, g, b, i, stake], instruction::delegate(&staked_entry));

    c.compare("UpdateEpochsStaked (staked)", "anyone_update_epochs_staked_tx.sh", &args![other, g, b, i, stake],
              instruction::update_epochs_staked(&staked_entry));
    c.compare("UpdateEpochsStaked (not staked)", "anyone_update_epochs_staked_tx.sh", &args![other, g, b, i, "none"],
              instruction::update_epochs_staked(&entry));

    // Special instructions.  When the metadata is not reauthorized, the builder gives the system program's address
    // (all zeroes) as the new metadata authority, which the program ignores.

    c.compare("ReAuthorize (metadata)", "special_reauthorize_tx.sh",
              &args![other, admin, user, g, b, i, stake, token, "true"],
              instruction::reauthorize(&staked_entry, &admin, &user, Some(&token), None));
    c.compare("ReAuthorize (stake account only, token account given)", "special_reauthorize_tx.sh",
              &args![other, admin, user, g, b, i, stake, shinobi_immortals::id::SYSTEM_PROGRAM, "false", token],
              instruction::reauthorize(&staked_entry, &admin, &user, None, Some(&token)));

    // Every instruction was compared
    let all_codes: BTreeSet<u8> = (0..=21).collect();
    assert_eq!(c.codes, all_codes, "not every instruction was compared");

    assert!(c.failures.is_empty(), "{} differences:\n{}", c.failures.len(), c.failures.join("\n"));
}


#[test]
fn builders_reject_invalid_inputs() {
    let mut rng = Rng::new("builders_reject_invalid_inputs");
    let admin = rng.address();
    let sha256s = [ rng.bytes32() ];

    assert_eq!(instruction::add_entries_to_block(&admin, 1, 2, &"x".repeat(201), None, 0, &sha256s, None).err(),
               Some(BuildError::UriTooLong));
    assert_eq!(instruction::add_entries_to_block(&admin, 1, 2, "u", None, 0, &vec![ [ 0; 32 ]; 256 ], None).err(),
               Some(BuildError::TooManyEntries));
    assert_eq!(instruction::add_whitelist_entries(&admin, 1, 2, &[], None).err(),
               Some(BuildError::InvalidWhitelistCount));
    let too_many: Vec<Address> = (0..(instruction::MAX_WHITELIST_ADD_ENTRIES + 1)).map(|_| rng.address()).collect();
    assert_eq!(instruction::add_whitelist_entries(&admin, 1, 2, &too_many, None).err(),
               Some(BuildError::InvalidWhitelistCount));
    assert_eq!(instruction::set_metadata_bytes(&admin, 1, 2, 0, 0, &vec![ 0; 65536 ]).err(),
               Some(BuildError::TooManyBytes));
    assert_eq!(instruction::reveal_entries(&admin, 1, 2, 0, &[ 0; 256 ]).err(), Some(BuildError::TooManyEntries));
}
