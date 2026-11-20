// Prints curve point vectors for is_on_curve() of sdk/js/shinobi_immortals_base.js.
//
// Usage: curve_points <seed>
//
// Each line is 32 bytes in hex, then "true" if solana-address's bytes_are_curve_point() says that they are a point on
// the ed25519 curve, or "false" if not.  That is exactly how Solana decides whether a hash is on the curve, and so
// cannot be a program derived address (bytes_are_curve_point is
// CompressedEdwardsY::from_slice(bytes).decompress().is_some(), of curve25519-dalek).
//
// The first 50 lines are always the same edge cases: every non-canonical y (2^255 - 19 through 2^255 - 1), and y = 0,
// 1, p - 1, 2, and p - 2, each with and without the sign bit (the top bit) set, and all 0xff and all 0x7f bytes.
// Version 1 of @solana/web3.js disagrees with Solana on some of these, because it rejects non-canonical encodings,
// which Solana accepts.  Then follow 1000 values made from the seed, about half of which are on the curve.

use shinobi_sdk_js_reference::{hex, seed_argument, Rng};

fn emit(bytes: &[u8; 32]) {
    println!("{} {}", hex(bytes), solana_address::bytes_are_curve_point(bytes));
}

// Returns the little-endian bytes of 2^255 - 19 + k, for small k (which may be negative)
fn p_plus(k: i32) -> [u8; 32] {
    let mut bytes = [0xffu8; 32];
    bytes[31] = 0x7f;
    bytes[0] = (0xed + k) as u8;
    bytes
}

fn main() {
    let seed = seed_argument();
    let mut rng = Rng::new(&seed, "curve_points");

    // The edge cases, each with the sign bit clear and set
    let mut edges: Vec<[u8; 32]> = Vec::new();
    // Every non-canonical y: p through 2^255 - 1
    for k in 0..19 {
        edges.push(p_plus(k));
    }
    // y = 0, 1, p - 1, 2, p - 2
    let small = |n: u8| -> [u8; 32] {
        let mut b = [0u8; 32];
        b[0] = n;
        b
    };
    edges.extend([ small(0), small(1), p_plus(-1), small(2), p_plus(-2) ]);
    for bytes in edges.iter() {
        let mut with_sign = *bytes;
        with_sign[31] |= 0x80;
        emit(bytes);
        emit(&with_sign);
    }
    emit(&[0xff; 32]);
    emit(&[0x7f; 32]);

    // Values made from the seed
    for _ in 0..1000 {
        emit(&rng.bytes32());
    }
}
