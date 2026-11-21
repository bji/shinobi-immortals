// Tests that the Rust crate's account structures (sdk/rust/src/state.rs) are laid out exactly as the program's C
// structures (program/inc), as the C compiler lays them out.
//
// c/layouts.c, compiled with the program's own headers, prints each structure's size and alignment, the bytes of an
// instance filled with random bytes, and every field's name, offset, size, and value in that instance.  This test
// reads the same bytes as the Rust structure, and checks that the Rust structure has the same size and alignment, and
// for every field of the C structure, a field of the same name at the same offset, of the same size, holding the same
// value.  The values check that each field's integer type has the C type's signedness, not just its size.

mod common;

use common::{from_hex, hex};
use shinobi_immortals::state::{EntryAuction, EntryOwned};
use shinobi_immortals::{Address, Authority, Bid, Block, BlockConfiguration, Entry, EntryMetadata, LevelMetadata,
                        ProgramState, Whitelist};
use std::collections::BTreeMap;
use std::mem::{align_of, offset_of, size_of, size_of_val};
use std::process::Command;

/// The value of a field, as c/layouts.c prints it: integers as decimal, and arrays and addresses as hex
trait Value {
    fn value(&self) -> String;
}

macro_rules! integer_value {
    ($($type:ty),*) => { $(impl Value for $type { fn value(&self) -> String { self.to_string() } })* };
}
integer_value!(u8, u16, u32, u64, i64);

impl<const N: usize> Value for [u8; N] {
    fn value(&self) -> String { hex(self) }
}

impl<const N: usize> Value for [u32; N] {
    fn value(&self) -> String { hex(bytemuck::bytes_of(self)) }
}

impl<const N: usize> Value for [Address; N] {
    fn value(&self) -> String { hex(bytemuck::bytes_of(self)) }
}

impl Value for Address {
    fn value(&self) -> String { hex(self.as_ref()) }
}

/// A field of a Rust structure: its offset, its size, and its value (None for a field that is itself a structure)
struct Field {
    offset: usize,
    size: usize,
    value: Option<String>,
}

/// The fields of a Rust structure, by name
type Fields = BTreeMap<String, Field>;

/// Adds to `fields` the field `field` (which may be a member of a member, such as auction.winning_bid_pubkey) of
/// `instance`, a `type`; `value` for an integer, array, or address field, or `structure` for one that is a structure
macro_rules! field {
    ($fields:expr, $type:ty, $instance:expr, $($field:ident).+, value) => {
        $fields.insert(stringify!($($field).+).replace(' ', ""), Field {
            offset: offset_of!($type, $($field).+),
            size: size_of_val(&$instance.$($field).+),
            value: Some($instance.$($field).+.value()),
        })
    };
    ($fields:expr, $type:ty, $instance:expr, $($field:ident).+, structure) => {
        $fields.insert(stringify!($($field).+).replace(' ', ""), Field {
            offset: offset_of!($type, $($field).+),
            size: size_of_val(&$instance.$($field).+),
            value: None,
        })
    };
}

/// Returns the size, alignment, and fields of the Rust structure named `name`, reading its instance from `bytes`
fn rust_structure(name: &str, bytes: &[u8]) -> (usize, usize, Fields) {
    let mut f = Fields::new();
    match name {
        "ProgramState" => {
            let x: ProgramState = bytemuck::pod_read_unaligned(bytes);
            field!(f, ProgramState, x, data_type, value);
            field!(f, ProgramState, x, admin_pubkey, value);
            (size_of::<ProgramState>(), align_of::<ProgramState>(), f)
        }
        "Authority" => {
            let x: Authority = bytemuck::pod_read_unaligned(bytes);
            field!(f, Authority, x, data_type, value);
            field!(f, Authority, x, mystery_escrow_lamports, value);
            (size_of::<Authority>(), align_of::<Authority>(), f)
        }
        "BlockConfiguration" => {
            let x: BlockConfiguration = bytemuck::pod_read_unaligned(bytes);
            field!(f, BlockConfiguration, x, group_number, value);
            field!(f, BlockConfiguration, x, block_number, value);
            field!(f, BlockConfiguration, x, total_entry_count, value);
            field!(f, BlockConfiguration, x, total_mystery_count, value);
            field!(f, BlockConfiguration, x, mystery_phase_duration, value);
            field!(f, BlockConfiguration, x, mystery_start_price_lamports, value);
            field!(f, BlockConfiguration, x, reveal_period_duration, value);
            field!(f, BlockConfiguration, x, minimum_price_lamports, value);
            field!(f, BlockConfiguration, x, has_auction, value);
            field!(f, BlockConfiguration, x, duration, value);
            field!(f, BlockConfiguration, x, final_start_price_lamports, value);
            field!(f, BlockConfiguration, x, whitelist_duration, value);
            field!(f, BlockConfiguration, x, expected_annual_reward_bps, value);
            (size_of::<BlockConfiguration>(), align_of::<BlockConfiguration>(), f)
        }
        "Block" => {
            let x: Block = bytemuck::pod_read_unaligned(bytes);
            field!(f, Block, x, data_type, value);
            field!(f, Block, x, config, structure);
            field!(f, Block, x, added_entries_count, value);
            field!(f, Block, x, block_start_timestamp, value);
            field!(f, Block, x, mysteries_sold_count, value);
            field!(f, Block, x, mystery_phase_end_timestamp, value);
            (size_of::<Block>(), align_of::<Block>(), f)
        }
        "LevelMetadata" => {
            let x: LevelMetadata = bytemuck::pod_read_unaligned(bytes);
            field!(f, LevelMetadata, x, form, value);
            field!(f, LevelMetadata, x, skill, value);
            field!(f, LevelMetadata, x, ki_factor, value);
            field!(f, LevelMetadata, x, name, value);
            field!(f, LevelMetadata, x, title, value);
            field!(f, LevelMetadata, x, uri, value);
            field!(f, LevelMetadata, x, uri_contents_sha256, value);
            (size_of::<LevelMetadata>(), align_of::<LevelMetadata>(), f)
        }
        "EntryMetadata" => {
            let x: EntryMetadata = bytemuck::pod_read_unaligned(bytes);
            field!(f, EntryMetadata, x, level_1_credits, value);
            field!(f, EntryMetadata, x, random, value);
            field!(f, EntryMetadata, x, level_metadata, structure);
            field!(f, EntryMetadata, x, ki_harvest_reduction_per_day, value);
            (size_of::<EntryMetadata>(), align_of::<EntryMetadata>(), f)
        }
        "Entry" => {
            let x: Entry = bytemuck::pod_read_unaligned(bytes);
            field!(f, Entry, x, data_type, value);
            field!(f, Entry, x, block_pubkey, value);
            field!(f, Entry, x, group_number, value);
            field!(f, Entry, x, block_number, value);
            field!(f, Entry, x, entry_index, value);
            field!(f, Entry, x, mint_pubkey, value);
            field!(f, Entry, x, token_pubkey, value);
            field!(f, Entry, x, metaplex_metadata_pubkey, value);
            field!(f, Entry, x, minimum_price_lamports, value);
            field!(f, Entry, x, has_auction, value);
            field!(f, Entry, x, duration, value);
            field!(f, Entry, x, non_auction_start_price_lamports, value);
            field!(f, Entry, x, reveal_sha256, value);
            field!(f, Entry, x, reveal_timestamp, value);
            field!(f, Entry, x, purchase_price_lamports, value);
            field!(f, Entry, x, refund_awarded, value);
            field!(f, Entry, x, reauthorized, value);
            field!(f, Entry, x, auction, structure);
            field!(f, Entry, x, auction.highest_bid_lamports, value);
            field!(f, Entry, x, auction.winning_bid_pubkey, value);
            field!(f, Entry, x, owned, structure);
            field!(f, Entry, x, owned.stake_account, value);
            field!(f, Entry, x, owned.last_ki_harvest_stake_account_lamports, value);
            field!(f, Entry, x, level, value);
            field!(f, Entry, x, metadata, structure);
            field!(f, Entry, x, most_recent_ki_applied_window, value);
            field!(f, Entry, x, ki_applied_this_window, value);
            field!(f, Entry, x, total_level_credits, value);
            field!(f, Entry, x, epochs_staked, value);
            field!(f, Entry, x, epochs_staked_counted_to_epoch, value);
            // The nested structures' own sizes, which the C program gives only as the sizes of these fields
            assert_eq!(size_of::<EntryAuction>(), size_of_val(&x.auction));
            assert_eq!(size_of::<EntryOwned>(), size_of_val(&x.owned));
            (size_of::<Entry>(), align_of::<Entry>(), f)
        }
        "Bid" => {
            let x: Bid = bytemuck::pod_read_unaligned(bytes);
            field!(f, Bid, x, data_type, value);
            field!(f, Bid, x, mint_pubkey, value);
            field!(f, Bid, x, bidder_pubkey, value);
            (size_of::<Bid>(), align_of::<Bid>(), f)
        }
        "Whitelist" => {
            let x: Whitelist = bytemuck::pod_read_unaligned(bytes);
            field!(f, Whitelist, x, data_type, value);
            field!(f, Whitelist, x, count, value);
            field!(f, Whitelist, x, entries, value);
            (size_of::<Whitelist>(), align_of::<Whitelist>(), f)
        }
        _ => panic!("c/layouts.c printed an unknown structure {}", name),
    }
}


/// Compiles and runs c/layouts.c, returning what it prints
fn c_layouts() -> serde_json::Value {
    let build_dir = std::env::temp_dir().join(format!("shinobi-sdk-rust-layouts-{}", std::process::id()));
    std::fs::create_dir_all(&build_dir).unwrap();
    let executable = build_dir.join("layouts");
    let status = Command::new("cc")
        .args([ "-Wall", "-Werror", "-I" ]).arg(common::source_root().join("program"))
        .arg("-o").arg(&executable)
        .arg(std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("c").join("layouts.c"))
        .status().expect("running cc (is a C compiler installed?)");
    assert!(status.success(), "compiling c/layouts.c failed");
    let output = Command::new(&executable).output().unwrap();
    std::fs::remove_dir_all(&build_dir).unwrap();
    assert!(output.status.success());
    serde_json::from_slice(&output.stdout).unwrap()
}


#[test]
fn structures_match_the_program() {
    let layouts = c_layouts();
    let structures = layouts.as_object().unwrap();
    // Every one of the program's account structures (and the structures within them) is checked
    let names: Vec<&str> = structures.keys().map(|k| k.as_str()).collect();
    assert_eq!(names.len(), 9, "c/layouts.c printed {:?}", names);

    let mut failures = Vec::new();
    for (name, c) in structures {
        let bytes = from_hex(c["bytes"].as_str().unwrap());
        let (size, align, rust_fields) = rust_structure(name, &bytes);

        if size != c["size"].as_u64().unwrap() as usize {
            failures.push(format!("{}: size {}, C has {}", name, size, c["size"]));
        }
        if align != c["align"].as_u64().unwrap() as usize {
            failures.push(format!("{}: alignment {}, C has {}", name, align, c["align"]));
        }

        let c_fields = c["fields"].as_array().unwrap();
        for c_field in c_fields {
            let field_name = c_field["name"].as_str().unwrap();
            let Some(rust_field) = rust_fields.get(field_name) else {
                failures.push(format!("{}.{}: not in the Rust structure", name, field_name));
                continue;
            };
            if rust_field.offset != c_field["offset"].as_u64().unwrap() as usize {
                failures.push(format!("{}.{}: offset {}, C has {}", name, field_name, rust_field.offset,
                                      c_field["offset"]));
            }
            if rust_field.size != c_field["size"].as_u64().unwrap() as usize {
                failures.push(format!("{}.{}: size {}, C has {}", name, field_name, rust_field.size,
                                      c_field["size"]));
            }
            let c_value = c_field.get("value").map(|v| v.as_str().unwrap().to_string());
            if rust_field.value != c_value {
                failures.push(format!("{}.{}: value {:?}, C has {:?}", name, field_name, rust_field.value, c_value));
            }
        }
        // And the Rust structure has no field that the C structure lacks (its padding fields are private, and so
        // are not listed)
        for field_name in rust_fields.keys() {
            if !c_fields.iter().any(|f| f["name"].as_str() == Some(field_name)) {
                failures.push(format!("{}.{}: not in the C structure", name, field_name));
            }
        }
    }
    assert!(failures.is_empty(), "{} differences:\n{}", failures.len(), failures.join("\n"));
}
