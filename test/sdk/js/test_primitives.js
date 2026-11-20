'use strict';

// Tests the primitives that sdk/js/shinobi_immortals_base.js implements itself, so that it needs no dependencies:
// SHA-256, base58, the ed25519 curve check, SHA-512, ed25519 signature verification, and program derived addresses.
// Each is checked against an independent implementation or published values:
//
// - SHA-256 and SHA-512 against Node's crypto module
// - base58 against the published Bitcoin base58 test vectors, and by round trips
// - the curve check against Solana's own (reference/src/bin/curve_points.rs)
// - signature verification against Solana's own (reference/src/bin/ed25519_signatures.rs), and against Node's
//   ed25519
//
// The vectors of Solana's curve check and signature verification are made by the Rust programs of reference/, which
// compute each expected result with the Solana crates themselves.  They are made anew on every run, from the run's
// seed: the same edge cases every time, and new random values.
// - program derived addresses, and every address derivation function, against "solxact pda"

const crypto = require("crypto");
const { Base, Results, random_bytes, random_int, random_address, require_tools, reference_vectors,
        solxact_pda } = require("./common.js");

const results = new Results("primitives");

require_tools(results.name, [ "solxact", "cargo" ]);


// SHA-256 ------------------------------------------------------------------------------------------------------------

// Every length up to 300 bytes crosses each padding boundary (55, 56, 63, 64 bytes, ...) several times
let mismatches = [ ];
for (let length = 0; length <= 300; length++) {
    let message = random_bytes(length);
    let expected = crypto.createHash("sha256").update(message).digest("hex");
    if (Base.bytes_to_hex(Base.sha256(message)) != expected) {
        mismatches.push(length);
    }
}
results.check("sha256 matches node crypto for every length from 0 to 300", mismatches.length == 0,
              "mismatched at lengths " + mismatches.join(", "));

for (let length of [ 1000, 4095, 4096, 4097, 100000 ]) {
    let message = random_bytes(length);
    results.check("sha256 matches node crypto for length " + length,
                  Base.bytes_to_hex(Base.sha256(message)) == crypto.createHash("sha256").update(message).digest("hex"));
}

// The FIPS 180-2 example, given as a plain array rather than a Buffer
results.check("sha256 of \"abc\"",
              Base.bytes_to_hex(Base.sha256([ 0x61, 0x62, 0x63 ])) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");


// Base58 -------------------------------------------------------------------------------------------------------------

// The base58 test vectors of Bitcoin Core (src/test/data/base58_encode_decode.json), plus the full alphabet
const BASE58_VECTORS = [
    [ "", "" ],
    [ "61", "2g" ],
    [ "626262", "a3gV" ],
    [ "636363", "aPEr" ],
    [ "73696d706c792061206c6f6e6720737472696e67", "2cFupjhnEsSn59qHXstmK2ffpLv2" ],
    [ "00eb15231dfceb60925886b67d065299925915aeb172c06647", "1NS17iag9jJgTHD1VXjvLCEnZuQ3rJDE9L" ],
    [ "516b6fcd0f", "ABnLTmg" ],
    [ "bf4f89001e670274dd", "3SEo3LWLoPntC" ],
    [ "572e4794", "3EFU7m" ],
    [ "ecac89cad93923c02321", "EJDM8drfXA6uyA" ],
    [ "10c8511e", "Rt5zm" ],
    [ "00000000000000000000", "1111111111" ],
    [ "000111d38e5fc9071ffcd20b4a763cc9ae4f252bb4e48fd66a835e252ada93ff480d6dd43dc62a641155a5",
      "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz" ]
];

for (let [ hex, encoded ] of BASE58_VECTORS) {
    results.check("base58_encode of " + (hex || "nothing"), Base.base58_encode(Base.hex_to_bytes(hex)) == encoded,
                  "got " + Base.base58_encode(Base.hex_to_bytes(hex)));
    results.check("base58_decode of " + (encoded || "nothing"), Base.bytes_to_hex(Base.base58_decode(encoded)) == hex);
}

// Round trips of random values of many lengths, including leading zero bytes (which base58 encodes specially)
mismatches = [ ];
for (let i = 0; i < 2000; i++) {
    let bytes = random_bytes(i % 50);
    for (let z = 0; (z < (i % 4)) && (z < bytes.length); z++) {
        bytes[z] = 0;
    }
    if (Base.bytes_to_hex(Base.base58_decode(Base.base58_encode(bytes))) != bytes.toString("hex")) {
        mismatches.push(bytes.toString("hex"));
    }
}
results.check("base58 round trips 2000 random values", mismatches.length == 0, mismatches.slice(0, 3).join(", "));

// The system program's address is 32 zero bytes
results.check("the system program address decodes to 32 zero bytes",
              Base.bytes_to_hex(Base.address_to_bytes(Base.g_system_program_address)) == "00".repeat(32));

results.check_throws("base58_decode rejects a 0", () => Base.base58_decode("abc0"), "Invalid base58");
results.check_throws("base58_decode rejects an O", () => Base.base58_decode("O"), "Invalid base58");
results.check_throws("base58_decode rejects an I", () => Base.base58_decode("I"), "Invalid base58");
results.check_throws("base58_decode rejects an l", () => Base.base58_decode("l"), "Invalid base58");
results.check_throws("base58_decode rejects a non-string", () => Base.base58_decode(5), "Not a base58 string");
results.check_throws("address_to_bytes rejects a 31 byte address",
                     () => Base.address_to_bytes(Base.base58_encode(random_bytes(31))), "Invalid address");
results.check_throws("address_to_bytes rejects a 33 byte address",
                     () => Base.address_to_bytes(Base.base58_encode(random_bytes(33))), "Invalid address");

// to_address accepts a string address, or an object with a toBase58 method (such as a @solana/web3.js PublicKey)
let address = random_address();
results.check("to_address returns a string address", Base.to_address(address) == address);
results.check("to_address accepts an object with toBase58", Base.to_address({ toBase58 : () => address }) == address);
results.check_throws("to_address rejects an invalid address", () => Base.to_address("not an address"), "base58");


// Curve check --------------------------------------------------------------------------------------------------------

// Each vector is 32 bytes (hex), and whether Solana's bytes_are_curve_point says that they are a point on the curve
let vector_lines = reference_vectors("curve_points");
mismatches = [ ];
for (let line of vector_lines) {
    let [ hex, expected ] = line.split(" ");
    if (String(Base.is_on_curve(Base.hex_to_bytes(hex))) != expected) {
        mismatches.push(hex);
    }
}
results.check("is_on_curve agrees with Solana on " + vector_lines.length + " vectors", mismatches.length == 0,
              "mismatched " + mismatches.slice(0, 3).join(", "));


// SHA-512 ------------------------------------------------------------------------------------------------------------

// Every length up to 300 bytes crosses each padding boundary (111, 112, 127, 128, ... bytes) at least twice
mismatches = [ ];
for (let length = 0; length <= 300; length++) {
    let message = random_bytes(length);
    if (Base.bytes_to_hex(Base.sha512(message)) != crypto.createHash("sha512").update(message).digest("hex")) {
        mismatches.push(length);
    }
}
results.check("sha512 matches node crypto for every length from 0 to 300", mismatches.length == 0,
              "mismatched at lengths " + mismatches.join(", "));

for (let length of [ 1000, 8191, 8192, 8193 ]) {
    let message = random_bytes(length);
    results.check("sha512 matches node crypto for length " + length,
                  Base.bytes_to_hex(Base.sha512(message)) == crypto.createHash("sha512").update(message).digest("hex"));
}

// The FIPS 180-2 example, given as a plain array rather than a Buffer
results.check("sha512 of \"abc\"",
              Base.bytes_to_hex(Base.sha512([ 0x61, 0x62, 0x63 ])) ==
              "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a" +
              "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");


// Ed25519 signature verification -------------------------------------------------------------------------------------

// Against the Solana runtime's own verification, on vectors that include every kind of signature that it rejects
// (see reference/src/bin/ed25519_signatures.rs).  Each vector is a public key, signature, and message (hex, or "-"
// for none), and whether Solana's Signature::verify accepts the signature.
vector_lines = reference_vectors("ed25519_signatures");
mismatches = [ ];
let accepted = 0;
for (let line of vector_lines) {
    let [ public_key, signature, message, expected ] = line.split(" ");
    let actual = Base.ed25519_verify(Base.hex_to_bytes(public_key), Base.hex_to_bytes(signature),
                                     (message == "-") ? [ ] : Base.hex_to_bytes(message));
    if (String(actual) != expected) {
        mismatches.push(line.substring(0, 80));
    }
    if (expected == "true") {
        accepted += 1;
    }
}
results.check("ed25519_verify agrees with the Solana runtime on " + vector_lines.length + " vectors (" + accepted +
              " valid)", mismatches.length == 0, "mismatched " + mismatches.slice(0, 3).join(", "));

// Against Node's ed25519: signatures of new keys verify, and do not once any bit of the signature, the public key, or
// the message is changed
let failures = [ ];
for (let i = 0; i < 30; i++) {
    let { privateKey, publicKey } = crypto.generateKeyPairSync("ed25519");
    // The public key's 32 bytes are the last 32 of its DER encoding
    let public_key = publicKey.export({ format : "der", type : "spki" }).subarray(-32);
    let message = random_bytes(i * 11);
    let signature = crypto.sign(null, message, privateKey);
    if (!Base.ed25519_verify(public_key, signature, message)) {
        failures.push("valid signature " + i + " rejected");
    }
    let bad_signature = Buffer.from(signature);
    bad_signature[random_int(64)] ^= 1 << random_int(8);
    if (Base.ed25519_verify(public_key, bad_signature, message)) {
        failures.push("changed signature " + i + " accepted");
    }
    let bad_public_key = Buffer.from(public_key);
    bad_public_key[random_int(32)] ^= 1 << random_int(8);
    if (Base.ed25519_verify(bad_public_key, signature, message)) {
        failures.push("changed public key " + i + " accepted");
    }
    if (message.length > 0) {
        let bad_message = Buffer.from(message);
        bad_message[random_int(message.length)] ^= 1 << random_int(8);
        if (Base.ed25519_verify(public_key, signature, bad_message)) {
            failures.push("changed message " + i + " accepted");
        }
    }
}
results.check("ed25519_verify agrees with node crypto on 30 new keys", failures.length == 0, failures.join(", "));

results.check("ed25519_verify rejects a 63 byte signature",
              !Base.ed25519_verify(new Uint8Array(32), new Uint8Array(63), [ ]));
results.check("ed25519_verify rejects a 31 byte public key",
              !Base.ed25519_verify(new Uint8Array(31), new Uint8Array(64), [ ]));


// Program derived addresses ------------------------------------------------------------------------------------------

// find_pda of random seeds (of random number and length) against solxact, for several programs
const PROGRAMS = [ Base.g_self_program_address, Base.g_metaplex_program_address,
                   Base.g_spl_associated_token_program_address, random_address() ];
mismatches = [ ];
for (let i = 0; i < 40; i++) {
    let seeds = [ ];
    let seed_args = [ ];
    for (let s = 0, count = 1 + random_int(4); s < count; s++) {
        let seed = random_bytes(1 + random_int(32));
        seeds.push(seed);
        seed_args.push("u8", ...seed);
    }
    let program = PROGRAMS[i % PROGRAMS.length];
    let expected = solxact_pda(program, seed_args);
    let actual = Base.find_pda(seeds, program);
    if ((actual[0] != expected[0]) || (actual[1] != expected[1])) {
        mismatches.push(expected.join(".") + " != " + actual.join("."));
    }
}
results.check("find_pda matches solxact pda for 40 random seed sets", mismatches.length == 0,
              mismatches.slice(0, 3).join(", "));

results.check_throws("find_pda rejects a seed longer than 32 bytes",
                     () => Base.find_pda([ random_bytes(33) ], Base.g_self_program_address), "longer than 32");
results.check_throws("find_pda rejects more than 16 seeds",
                     () => Base.find_pda(new Array(16).fill([ 1 ]), Base.g_self_program_address), "Too many seeds");

// Each address derivation function of the SDK, against the same derivation written as the scripts write it
const SELF = Base.g_self_program_address;
const METAPLEX = Base.g_metaplex_program_address;
for (let i = 0; i < 5; i++) {
    let group_number = random_int(0x100000000);
    let block_number = random_int(0x100000000);
    let entry_index = random_int(0x10000);
    let a = random_address();
    let b = random_address();
    let derivations = [
        [ "get_block_address", Base.get_block_address(group_number, block_number),
          solxact_pda(SELF, [ "u8", 10, "u32", group_number, "u32", block_number ]) ],
        [ "get_whitelist_address", Base.get_whitelist_address(a), solxact_pda(SELF, [ "u8", 9, "pubkey", a ]) ],
        [ "get_entry_mint_address", Base.get_entry_mint_address(a, entry_index),
          solxact_pda(SELF, [ "u8", 4, "pubkey", a, "u16", entry_index ]) ],
        [ "get_entry_address", Base.get_entry_address(a), solxact_pda(SELF, [ "u8", 11, "pubkey", a ]) ],
        [ "get_entry_token_address", Base.get_entry_token_address(a), solxact_pda(SELF, [ "u8", 5, "pubkey", a ]) ],
        [ "get_metaplex_metadata_address", Base.get_metaplex_metadata_address(a),
          solxact_pda(METAPLEX, [ "string", "metadata", "pubkey", METAPLEX, "pubkey", a ]) ],
        [ "get_associated_token_address", Base.get_associated_token_address(a, b),
          solxact_pda(Base.g_spl_associated_token_program_address,
                      [ "pubkey", a, "pubkey", Base.g_spl_token_program_address, "pubkey", b ]) ],
        [ "get_bid_marker_token_address", Base.get_bid_marker_token_address(a, b),
          solxact_pda(SELF, [ "u8", 8, "pubkey", a, "pubkey", b ]) ],
        [ "get_bid_address", Base.get_bid_address(a), solxact_pda(SELF, [ "u8", 6, "pubkey", a ]) ]
    ];
    for (let [ name, actual, expected ] of derivations) {
        results.check(name + " matches solxact pda (" + i + ")", actual == expected[0], actual + " != " + expected[0]);
    }
}


// Instruction helpers ------------------------------------------------------------------------------------------------

// The roles are those of @solana/kit's AccountRole; test_kit.js checks them against @solana/kit itself
address = random_address();
results.check("account_meta of a read-only account",
              Base.account_meta(address, false, false).role == Base.AccountRole.READONLY);
results.check("account_meta of a writable account",
              Base.account_meta(address, false, true).role == Base.AccountRole.WRITABLE);
results.check("account_meta of a read-only signer",
              Base.account_meta(address, true, false).role == Base.AccountRole.READONLY_SIGNER);
results.check("account_meta of a writable signer",
              Base.account_meta(address, true, true).role == Base.AccountRole.WRITABLE_SIGNER);
results.check("account_meta keeps the address", Base.account_meta(address, true, true).address == address);
results.check_throws("account_meta rejects an invalid address", () => Base.account_meta("x", false, false), "Invalid");

let instruction = Base.make_instruction([ Base.account_meta(address, true, true) ], [ 1, 2, 3 ]);
results.check("make_instruction gives the program address", instruction.programAddress == Base.g_self_program_address);
results.check("make_instruction gives Uint8Array data",
              (instruction.data instanceof Uint8Array) && (Base.bytes_to_hex(instruction.data) == "010203"));


results.finish();
