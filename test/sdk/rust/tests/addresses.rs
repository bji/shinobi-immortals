// Tests the Rust crate's addresses (sdk/rust/src/id.rs and pda.rs):
//
// - The fixed addresses, and the bump seeds of the program's own fixed accounts, against those that build_program.sh
//   compiles into the program
// - Each address derivation function against "solxact pda", with random inputs

mod common;

use common::{solxact_pda, Rng};
use shinobi_immortals::{id, pda, Address};
use std::collections::HashMap;

/// Returns the bytes of `NAME_PUBKEY_C_ARRAY="{1,2,...}"` lines of build_program.sh, as addresses by name, and its
/// `NAME_BUMP_SEED="n"` lines, as bump seeds by name
fn build_program() -> (HashMap<String, Address>, HashMap<String, u8>) {
    let text = std::fs::read_to_string(common::source_root().join("build_program.sh")).unwrap();
    let mut addresses = HashMap::new();
    let mut bump_seeds = HashMap::new();
    for line in text.lines() {
        if let Some((name, value)) = line.split_once("_PUBKEY_C_ARRAY=\"{") {
            let bytes: Vec<u8> = value.trim_end_matches("}\"").split(',').map(|b| b.trim().parse().unwrap()).collect();
            addresses.insert(name.to_string(), Address::new_from_array(bytes.try_into().unwrap()));
        }
        else if let Some((name, value)) = line.split_once("_BUMP_SEED=\"") {
            if !name.contains(' ') && !name.starts_with('-') {
                bump_seeds.insert(name.to_string(), value.trim_end_matches('"').parse().unwrap());
            }
        }
    }
    (addresses, bump_seeds)
}


#[test]
fn fixed_addresses_match_the_program() {
    let (compiled, bump_seeds) = build_program();

    // The crate's name for each address that build_program.sh compiles into the program
    let known: Vec<(&str, Address)> = vec![
        ("SELF_PROGRAM", id::PROGRAM_ID),
        ("SYSTEM_PROGRAM", id::SYSTEM_PROGRAM),
        ("METAPLEX_PROGRAM", id::METAPLEX_PROGRAM),
        ("SPL_TOKEN_PROGRAM", id::SPL_TOKEN_PROGRAM),
        ("SPLATA_PROGRAM", id::SPL_ASSOCIATED_TOKEN_PROGRAM),
        ("STAKE_PROGRAM", id::STAKE_PROGRAM),
        ("CLOCK_SYSVAR", id::CLOCK_SYSVAR),
        ("RENT_SYSVAR", id::RENT_SYSVAR),
        ("STAKE_HISTORY_SYSVAR", id::STAKE_HISTORY_SYSVAR),
        ("STAKE_CONFIG_SYSVAR", id::STAKE_CONFIG),
        ("SHINOBI_SYSTEMS_VOTE", id::SHINOBI_SYSTEMS_VOTE),
        ("STATE", id::PROGRAM_STATE),
        ("AUTHORITY", id::AUTHORITY),
        ("KI_MINT", id::KI_MINT),
        ("KI_METADATA", id::KI_METADATA),
        ("BID_MARKER_MINT", id::BID_MARKER_MINT),
        ("BID_MARKER_METADATA", id::BID_MARKER_METADATA),
    ];
    for (name, address) in &known {
        assert_eq!(compiled.get(*name), Some(address), "{} differs from build_program.sh", name);
    }
    // Every address that build_program.sh compiles in (but the superuser, which the crate does not know) is checked
    for name in compiled.keys() {
        assert!((name == "SUPERUSER") || known.iter().any(|(n, _)| n == name), "{} is not known to this test", name);
    }

    // The program's own fixed accounts are derived from single byte seeds, with these bump seeds
    for (name, seed, address, bump_seed) in [
        ("STATE", pda::PROGRAM_STATE_SEED, id::PROGRAM_STATE, id::PROGRAM_STATE_BUMP_SEED),
        ("AUTHORITY", pda::AUTHORITY_SEED, id::AUTHORITY, id::AUTHORITY_BUMP_SEED),
        ("KI_MINT", pda::KI_MINT_SEED, id::KI_MINT, id::KI_MINT_BUMP_SEED),
        ("BID_MARKER_MINT", pda::BID_MARKER_MINT_SEED, id::BID_MARKER_MINT, id::BID_MARKER_MINT_BUMP_SEED),
    ] {
        assert_eq!(bump_seeds.get(name), Some(&bump_seed), "{} bump seed differs from build_program.sh", name);
        assert_eq!(Address::find_program_address(&[ &[ seed ] ], &id::PROGRAM_ID), (address, bump_seed),
                   "{} is not the program derived address of its seed", name);
        // derive_address, which is cheap on chain, gives the same address from the bump seed
        assert_eq!(Address::derive_address(&[ &[ seed ] ], Some(bump_seed), &id::PROGRAM_ID), address);
    }

    // The metadata of the program's mints are where Metaplex puts them
    assert_eq!(pda::metaplex_metadata_address(&id::KI_MINT), id::KI_METADATA);
    assert_eq!(pda::metaplex_metadata_address(&id::BID_MARKER_MINT), id::BID_MARKER_METADATA);
}


#[test]
fn derivations_match_solxact() {
    let mut rng = Rng::new("derivations_match_solxact");
    let program = id::PROGRAM_ID;
    let s = |x: &dyn ToString| x.to_string();

    for _ in 0..5 {
        let (g, b, i) = (rng.u32(), rng.u32(), rng.u16());
        let (x, y) = (rng.address(), rng.address());
        let checks: Vec<(&str, (Address, u8), (Address, u8))> = vec![
            ("find_block_address", pda::find_block_address(g, b),
             solxact_pda(&program, &[ s(&"u8"), s(&10), s(&"u32"), s(&g), s(&"u32"), s(&b) ])),
            ("find_whitelist_address", pda::find_whitelist_address(&x),
             solxact_pda(&program, &[ s(&"u8"), s(&9), s(&"pubkey"), s(&x) ])),
            ("find_entry_mint_address", pda::find_entry_mint_address(&x, i),
             solxact_pda(&program, &[ s(&"u8"), s(&4), s(&"pubkey"), s(&x), s(&"u16"), s(&i) ])),
            ("find_entry_address", pda::find_entry_address(&x),
             solxact_pda(&program, &[ s(&"u8"), s(&11), s(&"pubkey"), s(&x) ])),
            ("find_entry_token_address", pda::find_entry_token_address(&x),
             solxact_pda(&program, &[ s(&"u8"), s(&5), s(&"pubkey"), s(&x) ])),
            ("find_bid_marker_token_address", pda::find_bid_marker_token_address(&x, &y),
             solxact_pda(&program, &[ s(&"u8"), s(&8), s(&"pubkey"), s(&x), s(&"pubkey"), s(&y) ])),
            ("find_bid_address", pda::find_bid_address(&x),
             solxact_pda(&program, &[ s(&"u8"), s(&6), s(&"pubkey"), s(&x) ])),
            ("find_metaplex_metadata_address", pda::find_metaplex_metadata_address(&x),
             solxact_pda(&id::METAPLEX_PROGRAM, &[ s(&"string"), s(&"metadata"), s(&"pubkey"),
                                                   s(&id::METAPLEX_PROGRAM), s(&"pubkey"), s(&x) ])),
            ("find_associated_token_address", pda::find_associated_token_address(&x, &y),
             solxact_pda(&id::SPL_ASSOCIATED_TOKEN_PROGRAM, &[ s(&"pubkey"), s(&x), s(&"pubkey"),
                                                               s(&id::SPL_TOKEN_PROGRAM), s(&"pubkey"), s(&y) ])),
        ];
        for (name, actual, expected) in checks {
            assert_eq!(actual, expected, "{} differs from solxact pda", name);
        }
    }
}
