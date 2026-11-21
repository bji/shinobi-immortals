// What the tests share: the source tree, seeded random values, the scripts and solxact, and making entries.

#![allow(dead_code)]

use shinobi_immortals::{pda, Address, Entry};
use std::path::PathBuf;
use std::process::{Command, Stdio};

/// The root of the source tree
pub fn source_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("..").join("..").join("..")
}


// Random values ------------------------------------------------------------------------------------------------------
//
// Random inputs come from a generator seeded by TEST_SEED (any string), or by the time if it is not set.  Each test
// prints its seed, which the test harness shows if the test fails; setting TEST_SEED to it repeats the test exactly.

/// A seeded random generator (xorshift64*)
pub struct Rng(u64);

impl Rng {
    /// Returns a generator for the test named `name`, printing its seed
    pub fn new(name: &str) -> Rng {
        let seed = std::env::var("TEST_SEED").unwrap_or_else(|_| {
            format!("{:016x}", std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()
                    as u64)
        });
        println!("{}: TEST_SEED={}", name, seed);
        // FNV-1a of the seed and the test's name, so that each test has its own values
        let mut hash: u64 = 0xcbf29ce484222325;
        for byte in seed.bytes().chain(name.bytes()) {
            hash ^= byte as u64;
            hash = hash.wrapping_mul(0x100000001b3);
        }
        Rng(hash | 1)
    }

    pub fn u64(&mut self) -> u64 {
        self.0 ^= self.0 >> 12;
        self.0 ^= self.0 << 25;
        self.0 ^= self.0 >> 27;
        self.0.wrapping_mul(0x2545f4914f6cdd1d)
    }

    pub fn u32(&mut self) -> u32 {
        (self.u64() >> 32) as u32
    }

    pub fn u16(&mut self) -> u16 {
        (self.u64() >> 48) as u16
    }

    /// A value from 0 to n - 1
    pub fn below(&mut self, n: u64) -> u64 {
        self.u64() % n
    }

    pub fn bytes(&mut self, n: usize) -> Vec<u8> {
        (0..n).map(|_| (self.u64() >> 56) as u8).collect()
    }

    pub fn bytes32(&mut self) -> [u8; 32] {
        self.bytes(32).try_into().unwrap()
    }

    pub fn address(&mut self) -> Address {
        Address::new_from_array(self.bytes32())
    }
}


// External tools -----------------------------------------------------------------------------------------------------

/// Runs the script `scripts/<name>` with `args`, returning what it writes to stdout (a transaction script writes an
/// unsigned transaction)
pub fn run_script(name: &str, args: &[String]) -> Vec<u8> {
    let output = Command::new("bash").arg(source_root().join("scripts").join(name)).args(args)
        .stderr(Stdio::inherit()).output().expect("running bash");
    assert!(output.status.success(), "{} {:?} failed", name, args);
    output.stdout
}

/// Runs solxact with `args` and `input` as its standard input, returning its standard output
pub fn solxact(args: &[&str], input: &[u8]) -> Vec<u8> {
    use std::io::Write;
    let mut child = Command::new("solxact").args(args).stdin(Stdio::piped()).stdout(Stdio::piped())
        .stderr(Stdio::inherit()).spawn().expect("running solxact (is it installed?)");
    child.stdin.take().unwrap().write_all(input).unwrap();
    let output = child.wait_with_output().unwrap();
    assert!(output.status.success(), "solxact {:?} failed", args);
    output.stdout
}

/// Returns the JSON that "solxact decode" makes of a transaction
pub fn solxact_decode(transaction: &[u8]) -> serde_json::Value {
    serde_json::from_slice(&solxact(&[ "decode" ], transaction)).expect("solxact decode output")
}

/// Returns the program derived address and bump seed that "solxact pda" finds for `program` and the seeds `seeds`
/// (given as solxact takes them, such as "u8", "10", "pubkey", "<address>")
pub fn solxact_pda(program: &Address, seeds: &[String]) -> (Address, u8) {
    // The seeds are given between "[" and "]"
    let mut args = vec![ "pda".to_string(), program.to_string(), "[".to_string() ];
    args.extend(seeds.iter().cloned());
    args.push("]".to_string());
    let args: Vec<&str> = args.iter().map(|s| s.as_str()).collect();
    let output = String::from_utf8(solxact(&args, &[])).unwrap();
    // solxact prints ADDRESS.BUMP
    let (address, bump) = output.trim().rsplit_once('.').expect("solxact pda output");
    (address.parse().expect("an address"), bump.parse().expect("a bump seed"))
}


// Entries ------------------------------------------------------------------------------------------------------------

/// Returns an entry, as read from its account, of entry `entry_index` of the block `block_number` of group
/// `group_number`, with the addresses that the program gives it; staked with `stake_account` if that is given
pub fn make_entry(group_number: u32, block_number: u32, entry_index: u16, stake_account: Option<&Address>) -> Entry {
    let block = pda::block_address(group_number, block_number);
    let mint = pda::entry_mint_address(&block, entry_index);
    let mut entry: Entry = bytemuck::Zeroable::zeroed();
    entry.data_type = 3;
    entry.block_pubkey = block;
    entry.group_number = group_number;
    entry.block_number = block_number;
    entry.entry_index = entry_index;
    entry.mint_pubkey = mint;
    entry.token_pubkey = pda::entry_token_address(&mint);
    entry.metaplex_metadata_pubkey = pda::metaplex_metadata_address(&mint);
    if let Some(stake_account) = stake_account {
        entry.owned.stake_account = *stake_account;
    }
    entry
}

/// Returns bytes as lower case hex
pub fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{:02x}", b)).collect()
}

/// Returns the bytes of a hex string
pub fn from_hex(hex: &str) -> Vec<u8> {
    (0..(hex.len() / 2)).map(|i| u8::from_str_radix(&hex[(i * 2)..((i * 2) + 2)], 16).unwrap()).collect()
}
