'use strict';

// Tests that sdk/js/shinobi_immortals_base.js agrees with the program it describes, by reading the program's own
// sources:
//
// - The instruction codes, data type values, WithdrawExcess account type codes, and the whitelist addition limit,
//   against the C source; and that there is an instruction builder for every instruction
// - The fixed addresses (programs, sysvars, and the program's own accounts) and bump seeds, against those that
//   build_program.sh compiles into the program
// - The account data decoders, against the structure layouts of program/inc, by decoding instances of those
//   structures made by layouts.c (compiled with the host C compiler)

const fs = require("fs");
const os = require("os");
const path = require("path");
const { execFileSync } = require("child_process");
const { SOURCE_ROOT, Base, Results, require_tools, to_json } = require("./common.js");

const results = new Results("program consistency");

require_tools(results.name, [ "cc" ]);


// Returns the contents of a file of the source tree
function source(relative_path)
{
    return fs.readFileSync(path.join(SOURCE_ROOT, relative_path), "utf8");
}


// Returns a name with underscores removed and in upper case, so that names written in different styles compare:
// "ReAuthorize", "REAUTHORIZE", and "reauthorize" are all "REAUTHORIZE"
function squash(name)
{
    return name.replace(/_/g, "").toUpperCase();
}


// Instruction codes --------------------------------------------------------------------------------------------------

// The Instruction enum of program/entrypoint.c
let c_instructions = new Map();
for (let match of source("program/entrypoint.c").matchAll(/^\s*Instruction_(\w+)\s*=\s*(\d+)/gm)) {
    c_instructions.set(squash(match[1]), parseInt(match[2]));
}
results.check("program/entrypoint.c defines instructions", c_instructions.size > 0);

// The INSTRUCTION_ constants of the SDK
let js_instructions = new Map(Object.keys(Base).filter((k) => k.startsWith("INSTRUCTION_"))
                              .map((k) => [ squash(k.substring("INSTRUCTION_".length)), Base[k] ]));

for (let [ name, code ] of c_instructions) {
    results.check("instruction " + name + " has code " + code + " in the SDK", js_instructions.get(name) === code,
                  "the SDK has " + js_instructions.get(name));
    // Every instruction has a builder, named make_<instruction>_instruction
    let builder = Object.keys(Base).find((k) => /^make_\w+_instruction$/.test(k) &&
                                         (squash(k.substring(5, k.length - 12)) == name));
    results.check("instruction " + name + " has a builder", (builder != null) && (typeof(Base[builder]) == "function"));
}
for (let name of js_instructions.keys()) {
    results.check("SDK instruction " + name + " exists in the program", c_instructions.has(name));
}

// The DataType enum of program/inc/data_type.h
let data_type_match = source("program/inc/data_type.h").match(/DataType_Authority\s*=\s*(\d+)/);
results.check("DATA_TYPE_AUTHORITY matches program/inc/data_type.h",
              (data_type_match != null) && (Base.DATA_TYPE_AUTHORITY == parseInt(data_type_match[1])));

// The WithdrawExcessAccountType enum of program/admin/admin_withdraw_excess.c
let withdraw_source = source("program/admin/admin_withdraw_excess.c");
let c_account_types = new Map([ ...withdraw_source.matchAll(/WithdrawExcessAccountType_(\w+)\s*=\s*(\d+)/g) ]
                              .map((m) => [ m[1], parseInt(m[2]) ]));
results.check("WithdrawExcessAccountType has the program's account types",
              to_json([ ...c_account_types.keys() ].sort()) ==
              to_json(Object.keys(Base.WithdrawExcessAccountType).sort()),
              "the program has " + [ ...c_account_types.keys() ].join(", "));
for (let [ name, code ] of c_account_types) {
    let js_type = Base.WithdrawExcessAccountType[name];
    results.check("WithdrawExcessAccountType " + name + " has code " + code,
                  (js_type != null) && (js_type.code == code) && (js_type.name == name));
}

// The whitelist addition limit of program/admin/admin_add_whitelist_entries.c
let whitelist_match = source("program/admin/admin_add_whitelist_entries.c")
    .match(/#define\s+MAX_WHITELIST_ADD_ENTRIES\s+(\d+)/);
results.check("MAX_WHITELIST_ADD_ENTRIES matches the program",
              (whitelist_match != null) && (Base.MAX_WHITELIST_ADD_ENTRIES == parseInt(whitelist_match[1])));


// Fixed addresses ----------------------------------------------------------------------------------------------------

// build_program.sh holds the addresses that are compiled into the program, as C byte arrays, and the bump seeds of
// its program derived addresses.  These are the SDK's names for them.
const ADDRESS_NAMES = {
    SELF_PROGRAM : "g_self_program_address",
    SYSTEM_PROGRAM : "g_system_program_address",
    METAPLEX_PROGRAM : "g_metaplex_program_address",
    SPL_TOKEN_PROGRAM : "g_spl_token_program_address",
    SPLATA_PROGRAM : "g_spl_associated_token_program_address",
    STAKE_PROGRAM : "g_stake_program_address",
    CLOCK_SYSVAR : "g_clock_sysvar_address",
    RENT_SYSVAR : "g_rent_sysvar_address",
    STAKE_HISTORY_SYSVAR : "g_stake_history_sysvar_address",
    STAKE_CONFIG_SYSVAR : "g_stake_config_address",
    EPOCH_REWARDS_SYSVAR : "g_epoch_rewards_sysvar_address",
    SHINOBI_SYSTEMS_VOTE : "g_shinobi_systems_vote_address",
    STATE : "g_program_state_address",
    AUTHORITY : "g_program_authority_address",
    KI_MINT : "g_ki_mint_address",
    KI_METADATA : "g_ki_metadata_address",
    BID_MARKER_MINT : "g_bid_marker_mint_address",
    BID_MARKER_METADATA : "g_bid_marker_metadata_address",
    REFERENCE_STAKE : "g_reference_stake_address"
};

// The seed of each of the program's own derived addresses, for checking bump seeds
const PDA_SEEDS = { STATE : 1, AUTHORITY : 2, KI_MINT : 3, BID_MARKER_MINT : 7, REFERENCE_STAKE : 12 };

let build_program = source("build_program.sh");
let compiled = new Map([ ...build_program.matchAll(/^(\w+)_PUBKEY_C_ARRAY="\{([\d,]+)\}"/gm) ]
                       .map((m) => [ m[1], Base.base58_encode(m[2].split(",").map(Number)) ]));
let bump_seeds = new Map([ ...build_program.matchAll(/^(\w+)_BUMP_SEED="(\d+)"/gm) ]
                         .map((m) => [ m[1], parseInt(m[2]) ]));

for (let [ name, js_name ] of Object.entries(ADDRESS_NAMES)) {
    results.check(js_name + " is the address compiled into the program as " + name,
                  compiled.has(name) && (Base[js_name] == compiled.get(name)),
                  "build_program.sh has " + compiled.get(name) + ", the SDK has " + Base[js_name]);
}
// Every address that build_program.sh compiles in (other than the superuser, which the SDK does not know) is checked
for (let name of compiled.keys()) {
    results.check("build_program.sh address " + name + " is known to this test",
                  (name == "SUPERUSER") || (name in ADDRESS_NAMES));
}
for (let [ name, seed ] of Object.entries(PDA_SEEDS)) {
    let [ address, bump_seed ] = Base.find_pda([ [ seed ] ], Base.g_self_program_address);
    results.check("bump seed of " + name + " matches build_program.sh",
                  (address == compiled.get(name)) && (bump_seed == bump_seeds.get(name)),
                  "build_program.sh has " + bump_seeds.get(name) + ", find_pda gives " + bump_seed);
}


// Account data layouts -----------------------------------------------------------------------------------------------

// Compile and run layouts.c, which prints instances of the program's structures and the values of their fields
let build_dir = fs.mkdtempSync(path.join(os.tmpdir(), "shinobi-sdk-js-layouts-"));
let layouts;
try {
    let executable = path.join(build_dir, "layouts");
    execFileSync("cc", [ "-Wall", "-Werror", "-I", path.join(SOURCE_ROOT, "program"), "-o", executable,
                         path.join(__dirname, "layouts.c") ]);
    layouts = JSON.parse(execFileSync(executable).toString());
}
finally {
    fs.rmSync(build_dir, { recursive : true, force : true });
}

results.check("DataType_Authority from the compiled headers matches DATA_TYPE_AUTHORITY",
              layouts.data_types.DataType_Authority == String(Base.DATA_TYPE_AUTHORITY));


// Returns a decoded value in the form that layouts.c prints it: addresses as "hex:" and their bytes, numbers and
// BigInts as decimal strings, and arrays and objects with their members converted
function printable(value, expected)
{
    if (Array.isArray(value)) {
        return value.map((v, i) => printable(v, (expected || [ ])[i]));
    }
    if ((value != null) && (typeof(value) == "object")) {
        return Object.fromEntries(Object.entries(value).map(([ k, v ]) => [ k, printable(v, (expected || { })[k]) ]));
    }
    if ((typeof(expected) == "string") && expected.startsWith("hex:")) {
        return "hex:" + Base.bytes_to_hex(Base.address_to_bytes(value));
    }
    return String(value);
}


// Compares every field that a decoder produces with the value that layouts.c set
function check_decoder(structure, decoded)
{
    let expected = layouts[structure].fields;
    let names = new Set([ ...Object.keys(expected), ...Object.keys(decoded) ]);
    for (let name of names) {
        if (!(name in expected)) {
            results.check(structure + "." + name + " is checked by layouts.c", false, "layouts.c does not set it");
            continue;
        }
        if (!(name in decoded)) {
            results.check(structure + "." + name + " is decoded", false, "the decoder does not produce it");
            continue;
        }
        let actual = printable(decoded[name], expected[name]);
        results.check(structure + "." + name + " is decoded correctly", to_json(actual) == to_json(expected[name]),
                      "decoded " + to_json(actual).substring(0, 200) + ", expected " +
                      to_json(expected[name]).substring(0, 200));
    }
}

check_decoder("block", Base.decode_block(Base.hex_to_bytes(layouts.block.bytes)));
check_decoder("entry", Base.decode_entry(Base.hex_to_bytes(layouts.entry.bytes)));
check_decoder("program_state", Base.decode_program_state(Base.hex_to_bytes(layouts.program_state.bytes)));
check_decoder("authority", Base.decode_authority(Base.hex_to_bytes(layouts.authority.bytes)));

// The decoders of the program state and authority accounts reject data that is not of those accounts
results.check("decode_program_state rejects short data", Base.decode_program_state(new Uint8Array(71)) == null);
results.check("decode_authority rejects data of the wrong size", Base.decode_authority(new Uint8Array(17)) == null);
let not_authority = Base.hex_to_bytes(layouts.authority.bytes);
not_authority[0] = 5;
results.check("decode_authority rejects data of the wrong data type", Base.decode_authority(not_authority) == null);


results.finish();
