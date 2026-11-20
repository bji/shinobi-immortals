// Prints ed25519 signature verification vectors for ed25519_verify() of sdk/js/shinobi_immortals_base.js.
//
// Usage: ed25519_signatures <seed>
//
// Each line is a public key (32 bytes), a signature (64 bytes), and a message (any number of bytes, or "-" for none),
// all in hex, then "true" if solana-signature's Signature::verify accepts the signature, or "false" if not.  That is
// how the Solana runtime verifies transaction signatures (it is VerificationKey::verify_dalek of the solana-ed25519
// crate, equivalent to ed25519-dalek's verify_strict).
//
// The keys, nonces, and messages are made from the seed; the kinds of vector are always the same.  They are, in
// order:
//
// 1. Valid signatures of random messages (of 0 to 295 bytes), each followed by: the signature with one bit flipped,
//    the public key with one bit flipped, and (but for the empty message) the message with one bit flipped
// 2. Signatures whose s is not less than the group order L (s + L, L, L - 1, and all 0xff bytes)
// 3. Every encoding (canonical or not, with either sign bit) of a point of small order as the public key, with R = B
//    and s = 1; and points of small order as R
// 4. Signatures whose R is the identity point and whose s is h a, which satisfy the verification equation, and are
//    rejected only because R has small order
// 5. Public keys and R values of mixed order (a point of the prime order subgroup plus a point of small order), which
//    are accepted exactly when the cofactorless verification equation holds, which it does for only some messages
// 6. Non-canonical encodings (y of p or more, either sign bit) as the public key and as R, and random bytes

#![allow(non_snake_case)]

use sha2::{Digest, Sha512};
use shinobi_sdk_js_reference::{hex, seed_argument, Rng};
use solana_ed25519::constants::{ED25519_BASEPOINT_POINT as B, EIGHT_TORSION};
use solana_ed25519::edwards::{CompressedEdwardsY, EdwardsPoint};
use solana_ed25519::scalar::Scalar;

// The group order L, little-endian
const L_BYTES: [u8; 32] = [
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10,
];

// Prints a vector, judged by Solana's own signature verification
fn emit(A: &[u8; 32], signature: &[u8; 64], message: &[u8]) {
    let valid = solana_signature::Signature::from(*signature).verify(A, message);
    println!("{} {} {} {}", hex(A), hex(signature), hex(message), valid);
}

// Returns a random scalar (reduced modulo L)
fn random_scalar(rng: &mut Rng) -> Scalar {
    let mut wide = [0u8; 64];
    wide.copy_from_slice(&rng.bytes(64));
    Scalar::from_bytes_mod_order_wide(&wide)
}

// Returns the challenge h: the SHA-512 hash of R, the public key, and the message, modulo L
fn challenge(R: &[u8; 32], A: &[u8; 32], message: &[u8]) -> Scalar {
    let mut hasher = Sha512::new();
    hasher.update(R);
    hasher.update(A);
    hasher.update(message);
    let mut wide = [0u8; 64];
    wide.copy_from_slice(&hasher.finalize());
    Scalar::from_bytes_mod_order_wide(&wide)
}

// Returns a signature made with secret scalar a and nonce r, with R given as the bytes R, for the public key given
// as the bytes A: s = r + h a.  This is a valid signature when R and A are the encodings of r B and a B; giving other
// bytes for R and A makes the invalid (or unusual) signatures of the vectors.
fn sign(a: &Scalar, r: &Scalar, R: [u8; 32], A: &[u8; 32], message: &[u8]) -> [u8; 64] {
    let s = r + (challenge(&R, A, message) * a);
    signature_of(&R, s.as_bytes())
}

// Returns the signature whose R and s are the given bytes
fn signature_of(R: &[u8; 32], s: &[u8; 32]) -> [u8; 64] {
    let mut signature = [0u8; 64];
    signature[..32].copy_from_slice(R);
    signature[32..].copy_from_slice(s);
    signature
}

// Returns the public key of secret scalar a
fn public_key(a: &Scalar) -> [u8; 32] {
    (B * a).compress().to_bytes()
}

// Returns every encoding (canonical or not, with either sign bit) of a point of small order
fn small_order_encodings() -> Vec<[u8; 32]> {
    let mut candidates: Vec<[u8; 32]> = Vec::new();
    // The canonical encodings of the 8 points of small order
    for T in EIGHT_TORSION.iter() {
        candidates.push(T.compress().to_bytes());
    }
    // The non-canonical encodings: y of p through 2^255 - 1, which are the same points as y of 0 through 18
    for k in 0u8..19 {
        let mut bytes = [0xffu8; 32];
        bytes[31] = 0x7f;
        bytes[0] = 0xed + k;
        candidates.push(bytes);
    }
    // Each with either sign bit, if it is a point of small order
    let mut encodings = Vec::new();
    for candidate in candidates {
        for sign in [0u8, 0x80] {
            let mut bytes = candidate;
            bytes[31] = (bytes[31] & 0x7f) | sign;
            let is_small = CompressedEdwardsY(bytes).decompress().map_or(false, |P| P.is_small_order());
            if is_small && !encodings.contains(&bytes) {
                encodings.push(bytes);
            }
        }
    }
    encodings
}

fn main() {
    let seed = seed_argument();
    let mut rng = Rng::new(&seed, "ed25519_signatures");

    // 1. Valid signatures, and each with one bit flipped in the signature, the public key, and the message
    for i in 0..60 {
        let a = random_scalar(&mut rng);
        let r = random_scalar(&mut rng);
        let A = public_key(&a);
        let message = rng.bytes(i * 5);
        let signature = sign(&a, &r, public_key(&r), &A, &message);
        emit(&A, &signature, &message);

        let mut changed_signature = signature;
        let bit = rng.below(512);
        changed_signature[bit / 8] ^= 1 << (bit % 8);
        emit(&A, &changed_signature, &message);

        let mut changed_A = A;
        let bit = rng.below(256);
        changed_A[bit / 8] ^= 1 << (bit % 8);
        emit(&changed_A, &signature, &message);

        if !message.is_empty() {
            let mut changed_message = message.clone();
            let bit = rng.below(message.len() * 8);
            changed_message[bit / 8] ^= 1 << (bit % 8);
            emit(&A, &signature, &changed_message);
        }
    }

    // 2. s not less than L: s + L (when that fits in 32 bytes), L, L - 1 (which is valid as s, though not for this
    //    signature), and all 0xff bytes
    for _ in 0..10 {
        let a = random_scalar(&mut rng);
        let r = random_scalar(&mut rng);
        let A = public_key(&a);
        let message = rng.bytes(20);
        let signature = sign(&a, &r, public_key(&r), &A, &message);
        let R: [u8; 32] = signature[..32].try_into().unwrap();

        // s + L, added as little-endian bytes
        let mut s_plus_L = [0u8; 32];
        let mut carry = 0u16;
        for j in 0..32 {
            let sum = (signature[32 + j] as u16) + (L_BYTES[j] as u16) + carry;
            s_plus_L[j] = sum as u8;
            carry = sum >> 8;
        }
        if carry == 0 {
            emit(&A, &signature_of(&R, &s_plus_L), &message);
        }

        emit(&A, &signature_of(&R, &L_BYTES), &message);
        let mut L_minus_1 = L_BYTES;
        L_minus_1[0] -= 1;
        emit(&A, &signature_of(&R, &L_minus_1), &message);
        emit(&A, &signature_of(&R, &[0xff; 32]), &message);
    }

    // 3. Small order public keys, with R = B and s = 1 (for which s B - h A = B exactly when h A is the identity), and
    //    small order R values with valid public keys
    let small = small_order_encodings();
    for (i, A) in small.iter().enumerate() {
        for n in 0..4 {
            let message = format!("message {}", n).into_bytes();
            emit(A, &signature_of(&B.compress().to_bytes(), Scalar::ONE.as_bytes()), &message);

            let a = random_scalar(&mut rng);
            let R = small[((i * 4) + n + 3) % small.len()];
            emit(&public_key(&a), &sign(&a, &Scalar::ZERO, R, &public_key(&a), &message), &message);
        }
    }

    // 4. R is the identity, and s = h a, so that s B - h A is the identity: the verification equation holds, and only
    //    the rejection of small order R values rejects these
    for n in 0..8 {
        let message = format!("identity {}", n).into_bytes();
        let a = random_scalar(&mut rng);
        let A = public_key(&a);
        let identity = EdwardsPoint::default().compress().to_bytes();
        emit(&A, &sign(&a, &Scalar::ZERO, identity, &A, &message), &message);
    }

    // 5. Mixed order public keys and R values (a B + T, or r B + T, for T of small order), signed as if they were of
    //    prime order.  The cofactorless verification equation then holds only when h T (or T itself) vanishes, so some
    //    of these are accepted and some are not.
    for i in 0..80 {
        let a = random_scalar(&mut rng);
        let r = random_scalar(&mut rng);
        let T = EIGHT_TORSION[1 + (i % 7)];
        let message = rng.bytes(16);

        let mixed_A = ((B * a) + T).compress().to_bytes();
        emit(&mixed_A, &sign(&a, &r, public_key(&r), &mixed_A, &message), &message);

        let A = public_key(&a);
        emit(&A, &sign(&a, &r, ((B * r) + T).compress().to_bytes(), &A, &message), &message);
    }

    // 6. Non-canonical encodings (y of p + k, for k of 0 through 18, with either sign bit) as the public key, and as
    //    R; then random bytes as public key, R, and s (with s below 2^252, so that it is less than L)
    for k in 0u8..19 {
        for sign_bit in [0u8, 0x80] {
            let mut non_canonical = [0xffu8; 32];
            non_canonical[31] = 0x7f | sign_bit;
            non_canonical[0] = 0xed + k;
            let message = [k, sign_bit];

            emit(&non_canonical, &signature_of(&B.compress().to_bytes(), Scalar::ONE.as_bytes()), &message);

            let a = random_scalar(&mut rng);
            let A = public_key(&a);
            let r = random_scalar(&mut rng);
            emit(&A, &sign(&a, &r, non_canonical, &A, &message), &message);
        }
    }
    for _ in 0..40 {
        let A = rng.bytes32();
        let R = rng.bytes32();
        let mut s = rng.bytes32();
        s[31] &= 0x0f;
        let message = rng.bytes(8);
        emit(&A, &signature_of(&R, &s), &message);
    }
}
