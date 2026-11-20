'use strict';

// shinobi_immortals_base.js
//
// The foundation of the Shinobi Immortals Javascript API.  This file holds everything that is needed to interact with
// the Shinobi Immortals program at the lowest level, without any network access or state:
//
// - The addresses of the program, of the accounts that it owns at fixed addresses, and of the programs and sysvars
//   that it uses
// - The instruction codes of the program
// - Functions that derive the addresses of the program's accounts (blocks, entries, whitelists, bids, etc)
// - Functions that decode the data of the program's accounts
// - Functions that mirror the program's calculations (prices, minimum bids, Ki harvests)
// - Functions that build the program's instructions
//
// This file has no dependencies.  Addresses are base58 strings, byte data is Uint8Array, and instructions are plain
// objects in the form used by @solana/kit (see "Instructions" below), so that they can be used directly with that
// library, or converted for use with version 1 of @solana/web3.js.
//
// Higher level functionality (querying the cluster, tracking entries and wallets, and signing and submitting
// transactions) is in shinobi_immortals.js, which is built on this file.  A project that only needs to build
// instructions, for example to include them in its own transactions, needs only this file.

const LAMPORTS_PER_SOL = 1000 * 1000 * 1000;

// Primitives ----------------------------------------------------------------------------------------------------------
//
// This file has no dependencies, so it implements the little that it needs itself: base58 encoding of addresses, the
// SHA-256 hash, and the check of whether 32 bytes are a point on the ed25519 curve, which are needed to derive program
// addresses; and the SHA-512 hash and ed25519 signature verification, with which a signature can be checked before a
// transaction is sent.  None of these handle secrets; they only work with public data (addresses, signatures, and
// messages), and there is nothing here that signs.


// The base58 alphabet, as used by Solana (and Bitcoin) for addresses
const BASE58_ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

// Map from each base58 character to its value
const BASE58_VALUES = new Map([ ...BASE58_ALPHABET ].map((c, i) => [ c, i ]));


// Returns the base58 string encoding of bytes (an array, Uint8Array, or Buffer)
function base58_encode(bytes)
{
    // Each leading zero byte is encoded as a leading "1"
    let zeros = 0;
    while ((zeros < bytes.length) && (bytes[zeros] == 0)) {
        zeros += 1;
    }

    // Convert the remaining bytes, a big-endian base 256 number, into base 58 digits, held least significant first.
    // Each byte multiplies the number so far by 256 and adds the byte.
    let digits = [ ];
    for (let i = zeros; i < bytes.length; i++) {
        let carry = bytes[i];
        for (let j = 0; j < digits.length; j++) {
            carry += digits[j] * 256;
            digits[j] = carry % 58;
            carry = Math.floor(carry / 58);
        }
        while (carry > 0) {
            digits.push(carry % 58);
            carry = Math.floor(carry / 58);
        }
    }

    // Emit the leading "1"s and then the digits, most significant first
    let result = "1".repeat(zeros);
    for (let i = digits.length - 1; i >= 0; i--) {
        result += BASE58_ALPHABET[digits[i]];
    }

    return result;
}


// Returns as a Uint8Array the bytes that the base58 string str encodes.  Throws if str is not base58.
function base58_decode(str)
{
    if (typeof(str) != "string") {
        throw new Error("Not a base58 string: " + str);
    }

    // Each leading "1" is a leading zero byte
    let zeros = 0;
    while ((zeros < str.length) && (str[zeros] == "1")) {
        zeros += 1;
    }

    // Convert the remaining characters, a big-endian base 58 number, into bytes, held least significant first.  Each
    // character multiplies the number so far by 58 and adds the character's value.
    let bytes = [ ];
    for (let i = zeros; i < str.length; i++) {
        let carry = BASE58_VALUES.get(str[i]);
        if (carry === undefined) {
            throw new Error("Invalid base58 string: " + str);
        }
        for (let j = 0; j < bytes.length; j++) {
            carry += bytes[j] * 58;
            bytes[j] = carry & 0xFF;
            carry >>= 8;
        }
        while (carry > 0) {
            bytes.push(carry & 0xFF);
            carry >>= 8;
        }
    }

    // The leading zero bytes, and then the bytes, most significant first
    let result = new Uint8Array(zeros + bytes.length);
    for (let i = 0; i < bytes.length; i++) {
        result[zeros + i] = bytes[bytes.length - 1 - i];
    }

    return result;
}


// Returns the address that address represents, as a base58 string.  address may be a base58 string, or any object
// with a toBase58() method (such as a PublicKey of version 1 of @solana/web3.js).  Throws if it is not a 32 byte
// address.
function to_address(address)
{
    if ((address != null) && (typeof(address) == "object") && (typeof(address.toBase58) == "function")) {
        address = address.toBase58();
    }

    // Decoding checks that the address is base58 and 32 bytes long
    address_to_bytes(address);

    return address;
}


// Returns as a Uint8Array the 32 bytes of the base58 string address.  Throws if it is not a 32 byte address.
function address_to_bytes(address)
{
    let bytes = base58_decode(address);

    if (bytes.length != 32) {
        throw new Error("Invalid address: " + address);
    }

    return bytes;
}


// Returns as a Uint8Array the UTF-8 encoding of str
function utf8_to_bytes(str)
{
    return new TextEncoder().encode(str);
}


// Returns the lowercase hex string of bytes (an array, Uint8Array, or Buffer)
function bytes_to_hex(bytes)
{
    let result = "";
    for (let i = 0; i < bytes.length; i++) {
        result += (bytes[i] & 0xFF).toString(16).padStart(2, "0");
    }
    return result;
}


// Returns as a Uint8Array the bytes of the hex string hex.  Throws if hex is not an even length string of hex digits.
function hex_to_bytes(hex)
{
    if ((typeof(hex) != "string") || !/^([0-9a-fA-F]{2})*$/.test(hex)) {
        throw new Error("Invalid hex string: " + hex);
    }

    let result = new Uint8Array(hex.length / 2);
    for (let i = 0; i < result.length; i++) {
        result[i] = parseInt(hex.substr(i * 2, 2), 16);
    }
    return result;
}


// The SHA-256 round constants: the first 32 bits of the fractional parts of the cube roots of the first 64 primes
const SHA256_K = new Uint32Array([
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
]);


// Returns as a Uint8Array the SHA-256 hash of bytes (an array, Uint8Array, or Buffer), as specified in FIPS 180-4
function sha256(bytes)
{
    // Rotates the 32 bit value x right by n bits
    const rotr = (x, n) => ((x >>> n) | (x << (32 - n)));

    // Pad the message: append a 1 bit (0x80), then zero bytes until 8 bytes short of a multiple of 64 bytes, then the
    // message length in bits as a big-endian 64 bit number
    let length = bytes.length;
    let padded_length = Math.ceil((length + 9) / 64) * 64;
    let message = new Uint8Array(padded_length);
    message.set(bytes);
    message[length] = 0x80;
    let bit_length = length * 8;
    let view = new DataView(message.buffer);
    view.setUint32(padded_length - 8, Math.floor(bit_length / 0x100000000));
    view.setUint32(padded_length - 4, bit_length >>> 0);

    // The initial hash value: the first 32 bits of the fractional parts of the square roots of the first 8 primes
    let hash = new Uint32Array([ 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 ]);

    // The message schedule; storing into a Uint32Array reduces each value modulo 2^32
    let w = new Uint32Array(64);

    // Process each 64 byte block
    for (let block = 0; block < padded_length; block += 64) {
        // The first 16 words of the schedule are the block's big-endian words; the rest are mixed from earlier words
        for (let t = 0; t < 16; t++) {
            w[t] = view.getUint32(block + (t * 4));
        }
        for (let t = 16; t < 64; t++) {
            let s0 = rotr(w[t - 15], 7) ^ rotr(w[t - 15], 18) ^ (w[t - 15] >>> 3);
            let s1 = rotr(w[t - 2], 17) ^ rotr(w[t - 2], 19) ^ (w[t - 2] >>> 10);
            w[t] = w[t - 16] + s0 + w[t - 7] + s1;
        }

        // The 64 rounds of compression, with "| 0" keeping each sum to 32 bits
        let [ a, b, c, d, e, f, g, h ] = hash;
        for (let t = 0; t < 64; t++) {
            let S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            let ch = (e & f) ^ (~e & g);
            let temp1 = (h + S1 + ch + SHA256_K[t] + w[t]) | 0;
            let S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            let maj = (a & b) ^ (a & c) ^ (b & c);
            let temp2 = (S0 + maj) | 0;
            h = g;
            g = f;
            f = e;
            e = (d + temp1) | 0;
            d = c;
            c = b;
            b = a;
            a = (temp1 + temp2) | 0;
        }

        // Add the compressed block into the hash
        hash[0] += a;
        hash[1] += b;
        hash[2] += c;
        hash[3] += d;
        hash[4] += e;
        hash[5] += f;
        hash[6] += g;
        hash[7] += h;
    }

    // The hash is the big-endian bytes of its 8 words
    let result = new Uint8Array(32);
    let result_view = new DataView(result.buffer);
    for (let i = 0; i < 8; i++) {
        result_view.setUint32(i * 4, hash[i]);
    }
    return result;
}


// The prime of the field over which the ed25519 curve is defined: 2^255 - 19
const ED25519_P = (1n << 255n) - 19n;

// The constant d of the ed25519 curve -x^2 + y^2 = 1 + d x^2 y^2, which is -121665/121666 modulo ED25519_P
const ED25519_D = 37095705934669439343138083508754565189542113879843219016388785533085940283555n;


// Returns base raised to the power exponent, modulo modulus, for non-negative BigInts
function mod_pow(base, exponent, modulus)
{
    let result = 1n;
    base %= modulus;
    while (exponent > 0n) {
        if (exponent & 1n) {
            result = (result * base) % modulus;
        }
        base = (base * base) % modulus;
        exponent >>= 1n;
    }
    return result;
}


// Returns true if the 32 bytes are the compressed form of a point on the ed25519 curve, exactly as the Solana runtime
// decides it when deriving program addresses (it uses curve25519-dalek's CompressedEdwardsY::decompress).  A
// compressed point is the little-endian y coordinate, whose top bit is replaced by the sign of x.  It is a point on
// the curve if there is an x for that y: if x^2 = (y^2 - 1) / (d y^2 + 1) has a solution, which is when the right hand
// side is zero or a square modulo p.  Since the division is by a nonzero number (d y^2 + 1 is never zero, because -1/d
// is not a square), it is a square exactly when u v is, for u = y^2 - 1 and v = d y^2 + 1; and by Euler's criterion, a
// number is a nonzero square modulo p exactly when raising it to (p - 1) / 2 gives 1.
function is_on_curve(bytes)
{
    // Read the little-endian y coordinate, ignoring the top bit (the sign of x), and reduce it modulo p as
    // curve25519-dalek does
    let y = 0n;
    for (let i = 31; i >= 0; i--) {
        y = (y << 8n) | BigInt(bytes[i]);
    }
    y = (y & ((1n << 255n) - 1n)) % ED25519_P;

    let y2 = (y * y) % ED25519_P;
    let u = (y2 + ED25519_P - 1n) % ED25519_P;
    let v = ((ED25519_D * y2) + 1n) % ED25519_P;

    let legendre = mod_pow((u * v) % ED25519_P, (ED25519_P - 1n) / 2n, ED25519_P);

    return (legendre == 0n) || (legendre == 1n);
}


// SHA-512 and ed25519 signature verification -------------------------------------------------------------------------
//
// Signature verification is needed so that a signature can be checked before a transaction is sent, which catches a
// wrong or mistyped signature (for example one pasted back from an offline signing tool) with a clear error rather
// than a rejected transaction.  It only ever handles public data (addresses, signatures, and messages), never a
// secret key, so it has no need to be constant time; it favors simplicity and readability over speed.  It takes a
// few milliseconds per signature.
//
// The verification is exactly that of the Solana runtime, which verifies transaction signatures with
// VerificationKey::verify_dalek of the solana-ed25519 crate (equivalent to ed25519-dalek's verify_strict).  Any
// signature that it accepts is also accepted by the more permissive ZIP-215 rules that Solana is moving to
// (SIMD-0376).  BigInts are used throughout, for both the 64 bit words of SHA-512 and the 255 bit field elements of
// the curve.


// Returns the integer k-th root of the non-negative BigInt n: the largest r for which r^k <= n, found by Newton's
// method starting from a power of two that is known to be too large
function integer_root(n, k)
{
    if (n == 0n) {
        return 0n;
    }
    let r = 1n << BigInt(Math.ceil(n.toString(2).length / Number(k)));
    while (true) {
        let next = (((k - 1n) * r) + (n / (r ** (k - 1n)))) / k;
        if (next >= r) {
            return r;
        }
        r = next;
    }
}


// The first 80 primes, from which the SHA-512 constants are derived
const SHA512_PRIMES = (() => {
    let primes = [ ];
    for (let n = 2n; primes.length < 80; n++) {
        if (primes.every((p) => (n % p) != 0n)) {
            primes.push(n);
        }
    }
    return primes;
})();

// The SHA-512 round constants: the first 64 bits of the fractional parts of the cube roots of the first 80 primes
// (FIPS 180-4 section 4.2.3).  They are computed here rather than listed, as the integer cube root of p * 2^192 is
// the cube root of p to 64 binary places; test_primitives.js checks SHA-512 against Node's own implementation.
const SHA512_K = SHA512_PRIMES.map((p) => BigInt.asUintN(64, integer_root(p << 192n, 3n)));

// The SHA-512 initial hash value: the first 64 bits of the fractional parts of the square roots of the first 8 primes
// (FIPS 180-4 section 5.3.5)
const SHA512_H = SHA512_PRIMES.slice(0, 8).map((p) => BigInt.asUintN(64, integer_root(p << 128n, 2n)));


// Returns as a Uint8Array the SHA-512 hash of bytes (an array, Uint8Array, or Buffer), as specified in FIPS 180-4
function sha512(bytes)
{
    const MASK = (1n << 64n) - 1n;

    // Rotates the 64 bit value x right by n bits
    const rotr = (x, n) => ((x >> n) | (x << (64n - n))) & MASK;

    // Pad the message: append a 1 bit (0x80), then zero bytes until 16 bytes short of a multiple of 128 bytes, then
    // the message length in bits as a big-endian 128 bit number (of which only the low 64 bits can be nonzero here)
    let length = bytes.length;
    let padded_length = Math.ceil((length + 17) / 128) * 128;
    let message = new Uint8Array(padded_length);
    message.set(bytes);
    message[length] = 0x80;
    let view = new DataView(message.buffer);
    view.setBigUint64(padded_length - 8, BigInt(length) * 8n);

    let hash = SHA512_H.slice();

    // The message schedule
    let w = new Array(80);

    // Process each 128 byte block
    for (let block = 0; block < padded_length; block += 128) {
        // The first 16 words of the schedule are the block's big-endian words; the rest are mixed from earlier words
        for (let t = 0; t < 16; t++) {
            w[t] = view.getBigUint64(block + (t * 8));
        }
        for (let t = 16; t < 80; t++) {
            let s0 = rotr(w[t - 15], 1n) ^ rotr(w[t - 15], 8n) ^ (w[t - 15] >> 7n);
            let s1 = rotr(w[t - 2], 19n) ^ rotr(w[t - 2], 61n) ^ (w[t - 2] >> 6n);
            w[t] = (w[t - 16] + s0 + w[t - 7] + s1) & MASK;
        }

        // The 80 rounds of compression, with "& MASK" keeping each sum to 64 bits
        let [ a, b, c, d, e, f, g, h ] = hash;
        for (let t = 0; t < 80; t++) {
            let S1 = rotr(e, 14n) ^ rotr(e, 18n) ^ rotr(e, 41n);
            let ch = (e & f) ^ (~e & MASK & g);
            let temp1 = (h + S1 + ch + SHA512_K[t] + w[t]) & MASK;
            let S0 = rotr(a, 28n) ^ rotr(a, 34n) ^ rotr(a, 39n);
            let maj = (a & b) ^ (a & c) ^ (b & c);
            let temp2 = (S0 + maj) & MASK;
            h = g;
            g = f;
            f = e;
            e = (d + temp1) & MASK;
            d = c;
            c = b;
            b = a;
            a = (temp1 + temp2) & MASK;
        }

        // Add the compressed block into the hash
        let compressed = [ a, b, c, d, e, f, g, h ];
        hash = hash.map((value, i) => (value + compressed[i]) & MASK);
    }

    // The hash is the big-endian bytes of its 8 words
    let result = new Uint8Array(64);
    let result_view = new DataView(result.buffer);
    for (let i = 0; i < 8; i++) {
        result_view.setBigUint64(i * 8, hash[i]);
    }
    return result;
}


// The order of the ed25519 base point (the prime order of the subgroup in which signatures are made):
// 2^252 + 27742317777372353535851937790883648493
const ED25519_L = (1n << 252n) + 27742317777372353535851937790883648493n;

// A square root of -1 modulo ED25519_P: 2^((p - 1) / 4)
const ED25519_SQRT_M1 = mod_pow(2n, (ED25519_P - 1n) / 4n, ED25519_P);


// Returns x reduced to the range 0 to ED25519_P - 1 (x may be negative)
function fe(x)
{
    x %= ED25519_P;
    return (x < 0n) ? (x + ED25519_P) : x;
}


// Returns the BigInt whose little-endian bytes are bytes
function le_bytes_to_bigint(bytes)
{
    let n = 0n;
    for (let i = bytes.length - 1; i >= 0; i--) {
        n = (n << 8n) | BigInt(bytes[i]);
    }
    return n;
}


// Points of the curve are held in extended twisted Edwards coordinates [ X, Y, Z, T ], which represent the point
// x = X/Z, y = Y/Z, with x y = T/Z.  These allow points to be added without any division.

// The identity point (x = 0, y = 1)
const ED25519_IDENTITY = [ 0n, 1n, 1n, 0n ];


// Returns the sum of points p and q.  This is the "add-2008-hwcd-3" formula for twisted Edwards curves with a = -1
// (Hisil, Wong, Carter, and Dawson, "Twisted Edwards Curves Revisited", 2008), which is complete for ed25519: it gives
// the correct sum for every pair of points, including when p and q are the same point or the identity, so it is also
// used for doubling.
function point_add(p, q)
{
    let [ x1, y1, z1, t1 ] = p;
    let [ x2, y2, z2, t2 ] = q;
    let a = fe((y1 - x1) * (y2 - x2));
    let b = fe((y1 + x1) * (y2 + x2));
    let c = fe(t1 * 2n * ED25519_D * t2);
    let d = fe(z1 * 2n * z2);
    let e = b - a;
    let f = d - c;
    let g = d + c;
    let h = b + a;
    return [ fe(e * f), fe(g * h), fe(f * g), fe(e * h) ];
}


// Returns the negation of point p: (-x, y)
function point_negate(p)
{
    return [ fe(-p[0]), p[1], p[2], fe(-p[3]) ];
}


// Returns the point n p, for a non-negative BigInt n, by doubling and adding over the bits of n from the most
// significant down
function point_multiply(n, p)
{
    let result = ED25519_IDENTITY;
    for (let bit = BigInt(n.toString(2).length) - 1n; bit >= 0n; bit--) {
        result = point_add(result, result);
        if ((n >> bit) & 1n) {
            result = point_add(result, p);
        }
    }
    return result;
}


// Returns true if points p and q are the same point.  x1 = x2 exactly when X1/Z1 = X2/Z2, which is when
// X1 Z2 = X2 Z1, and likewise for y.
function point_equals(p, q)
{
    return (fe(p[0] * q[2]) == fe(q[0] * p[2])) && (fe(p[1] * q[2]) == fe(q[1] * p[2]));
}


// Returns true if point p has small order: if 8 p (8 being the curve's cofactor) is the identity, as
// curve25519-dalek's EdwardsPoint::is_small_order decides it.  There are 8 such points.
function point_is_small_order(p)
{
    let p8 = point_add(p, p);
    p8 = point_add(p8, p8);
    p8 = point_add(p8, p8);
    return point_equals(p8, ED25519_IDENTITY);
}


// Returns the point whose compressed form is the 32 bytes, or null if they are not the compressed form of a point on
// the curve, exactly as curve25519-dalek's CompressedEdwardsY::decompress does it (as is_on_curve also follows).
// Like that function, this accepts the non-canonical forms: a y coordinate of ED25519_P or more (which is reduced
// modulo ED25519_P), and a sign bit of 1 when x is 0.  A compressed point is the little-endian y coordinate, whose top
// bit is replaced by the sign (lowest bit) of x.
function point_decompress(bytes)
{
    let y = fe(le_bytes_to_bigint(bytes) & ((1n << 255n) - 1n));
    let sign = BigInt(bytes[31] >> 7);

    // x^2 = u / v, for u = y^2 - 1 and v = d y^2 + 1.  The candidate root of u / v is computed without a division as
    // x = u v^3 (u v^7)^((p - 5) / 8) (RFC 8032 section 5.1.3).  Then v x^2 is either u (x is a root), -u (x times
    // the square root of -1 is a root), or neither (u / v is not a square, and there is no point with this y).
    let y2 = fe(y * y);
    let u = fe(y2 - 1n);
    let v = fe((ED25519_D * y2) + 1n);
    let v3 = fe(v * v * v);
    let x = fe(u * v3 * mod_pow(fe(u * v3 * v3 * v), (ED25519_P - 5n) / 8n, ED25519_P));
    let vx2 = fe(v * x * x);
    if (vx2 == fe(-u)) {
        x = fe(x * ED25519_SQRT_M1);
    }
    else if (vx2 != u) {
        return null;
    }

    // Choose the root whose lowest bit is the sign bit.  When x is 0, both roots are 0, and the sign bit is ignored.
    if ((x & 1n) != sign) {
        x = fe(-x);
    }

    return [ x, y, 1n, fe(x * y) ];
}


// Returns the 32 byte (canonical) compressed form of point p: the little-endian y coordinate, with the lowest bit of
// x as the top bit
function point_compress(p)
{
    // Convert to affine coordinates, dividing by Z (multiplying by its inverse Z^(p - 2), by Fermat's little theorem)
    let z_inverse = mod_pow(p[2], ED25519_P - 2n, ED25519_P);
    let x = fe(p[0] * z_inverse);
    let y = fe(p[1] * z_inverse);

    let bytes = new Uint8Array(32);
    for (let i = 0; i < 32; i++) {
        bytes[i] = Number((y >> BigInt(i * 8)) & 0xffn);
    }
    bytes[31] |= Number(x & 1n) << 7;
    return bytes;
}


// The ed25519 base point B: the point whose y is 4/5 and whose x is even
const ED25519_BASE_POINT = point_decompress((() => {
    let y = fe(4n * mod_pow(5n, ED25519_P - 2n, ED25519_P));
    let bytes = new Uint8Array(32);
    for (let i = 0; i < 32; i++) {
        bytes[i] = Number((y >> BigInt(i * 8)) & 0xffn);
    }
    return bytes;
})());


// Returns true if signature (64 bytes) is a valid ed25519 signature of message (bytes) by the key whose public key
// (32 bytes, the same bytes as the key's address) is public_key.  Each argument may be an array, Uint8Array, or
// Buffer.
//
// This is the verification of the Solana runtime (VerificationKey::verify_dalek of the solana-ed25519 crate), step by
// step.  The signature is R (its first 32 bytes, a compressed point) and s (its last 32 bytes, a little-endian
// number); it is valid when R is the compressed form of s B - h A, where A is the public key's point, B is the base
// point, and h is the SHA-512 hash of R, the public key, and the message, as a number modulo ED25519_L.  On top of that
// basic check, the Solana runtime (like ed25519-dalek's verify_strict) rejects public keys and R values that are
// points of small order, which would otherwise allow signatures that are valid for more than one message, and s values
// that are not less than ED25519_L, which would otherwise allow more than one signature for one message.
function ed25519_verify(public_key, signature, message)
{
    if ((public_key.length != 32) || (signature.length != 64)) {
        return false;
    }
    let r_bytes = Uint8Array.from(signature.slice(0, 32));

    // The public key must be a point, not of small order
    let a = point_decompress(public_key);
    if ((a == null) || point_is_small_order(a)) {
        return false;
    }

    // R must be a point, not of small order
    let r = point_decompress(r_bytes);
    if ((r == null) || point_is_small_order(r)) {
        return false;
    }

    // s must be less than ED25519_L
    let s = le_bytes_to_bigint(signature.slice(32, 64));
    if (s >= ED25519_L) {
        return false;
    }

    // h is the hash of R, the public key, and the message, each as given
    let h = le_bytes_to_bigint(sha512([ ...r_bytes, ...public_key, ...message ])) % ED25519_L;

    // s B - h A must compress to exactly the bytes of R
    let expected_r = point_compress(point_add(point_multiply(s, ED25519_BASE_POINT),
                                              point_multiply(h, point_negate(a))));

    return expected_r.every((byte, i) => byte == r_bytes[i]);
}


// The most seeds, and the longest seed, that a program derived address may have
const MAX_SEEDS = 16;
const MAX_SEED_LENGTH = 32;


// Returns the program derived address of the seeds (each an array, Uint8Array, or Buffer of bytes) for the program
// at program_address, or null if those seeds give a point on the ed25519 curve (which cannot be a program derived
// address).  This is the Solana runtime's create_program_address.
function create_program_address(seeds, program_address)
{
    if (seeds.length > MAX_SEEDS) {
        throw new Error("Too many seeds");
    }

    // Hash the seeds, then the program's address, then the marker "ProgramDerivedAddress"
    let parts = [ ];
    for (let seed of seeds) {
        if (seed.length > MAX_SEED_LENGTH) {
            throw new Error("Seed is longer than " + MAX_SEED_LENGTH + " bytes");
        }
        parts.push(...seed);
    }
    parts.push(...address_to_bytes(program_address));
    parts.push(...utf8_to_bytes("ProgramDerivedAddress"));

    let hash = sha256(parts);

    return is_on_curve(hash) ? null : base58_encode(hash);
}


// Returns [ address, bump_seed ]: the program derived address of the seeds for the program at program_address, found
// as the Solana runtime's find_program_address does, by appending to the seeds a single byte "bump seed", trying each
// value from 255 down until one gives a valid program derived address.
function find_pda(seeds, program_address)
{
    for (let bump_seed = 255; bump_seed >= 0; bump_seed--) {
        let address = create_program_address(seeds.concat([ [ bump_seed ] ]), program_address);
        if (address != null) {
            return [ address, bump_seed ];
        }
    }

    throw new Error("No program derived address found");
}


// Instructions --------------------------------------------------------------------------------------------------------
//
// Instructions are plain objects in the form used by @solana/kit, so that they can be passed directly to its
// appendTransactionMessageInstruction:
//
// {
//     programAddress : the address of the program, as a base58 string,
//     accounts : an array of { address : base58 string, role : one of the AccountRole values },
//     data : Uint8Array
// }
//
// Projects that use version 1 of @solana/web3.js can convert them with:
//
//     new TransactionInstruction({
//         programId : new PublicKey(instruction.programAddress),
//         keys : instruction.accounts.map((a) => ({ pubkey : new PublicKey(a.address),
//                                                   isSigner : (a.role & 2) != 0,
//                                                   isWritable : (a.role & 1) != 0 })),
//         data : Buffer.from(instruction.data) });


// The role of an account in an instruction, with the same values as AccountRole of @solana/kit: bit 1 means that the
// account signs, and bit 0 that it is writable
const AccountRole = Object.freeze({
    READONLY        : 0,
    WRITABLE        : 1,
    READONLY_SIGNER : 2,
    WRITABLE_SIGNER : 3
});


// Returns the description of an account in an instruction: { address, role }, which is an AccountMeta of
// @solana/kit.  address may be anything that to_address accepts.
function account_meta(address, is_signer, is_writable)
{
    return { address : to_address(address),
             role : (is_signer ? AccountRole.READONLY_SIGNER : AccountRole.READONLY) | (is_writable ? 1 : 0) };
}


// Returns an instruction of this program with the given accounts and data (an array of bytes)
function make_instruction(accounts, data)
{
    return { programAddress : g_self_program_address, accounts : accounts, data : Uint8Array.from(data) };
}


// Addresses -----------------------------------------------------------------------------------------------------------

const g_system_program_address = "11111111111111111111111111111111";

const g_self_program_address = "Shin1cdrR1pmemXZU3yDV3PnQ48SX9UmrtHF4GbKzWG";

const g_metaplex_program_address = "metaqbxxUerdq28cj1RbAWkYQm3ybzjb6a8bt518x1s";

const g_spl_token_program_address = "TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA";

const g_spl_associated_token_program_address = "ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL";

const g_stake_program_address = "Stake11111111111111111111111111111111111111";

const g_config_program_address = "Config1111111111111111111111111111111111111";

const g_validator_info_address = "Va1idator1nfo111111111111111111111111111111";

const g_program_authority_address = find_pda([ [ 2 ] ], g_self_program_address)[0];

const g_program_state_address = find_pda([ [ 1 ] ], g_self_program_address)[0];

// The Shinobi Systems vote account, which is compiled into the program (see GNUmakefile).  It is the only validator
// that the program delegates stake accounts to.
const g_shinobi_systems_vote_address = "BLADE1qNA1uNjRgER6DtUFf7FU3c1TWLLdpPeEcKatZ2";

const g_ki_mint_address = find_pda([ [ 3 ] ], g_self_program_address)[0];

const g_ki_metadata_address = get_metaplex_metadata_address(g_ki_mint_address);

const g_bid_marker_mint_address = find_pda([ [ 7 ] ], g_self_program_address)[0];

const g_bid_marker_metadata_address = get_metaplex_metadata_address(g_bid_marker_mint_address);

const g_rent_sysvar_address = "SysvarRent111111111111111111111111111111111";

const g_clock_sysvar_address = "SysvarC1ock11111111111111111111111111111111";

const g_stake_history_sysvar_address = "SysvarStakeHistory1111111111111111111111111";

const g_stake_config_address = "StakeConfig11111111111111111111111111111111";

// Instruction codes of the program's instructions (see program/entrypoint.c)
const INSTRUCTION_INITIALIZE = 0;
const INSTRUCTION_SET_ADMIN = 1;
const INSTRUCTION_CREATE_BLOCK = 2;
const INSTRUCTION_ADD_ENTRIES_TO_BLOCK = 3;
const INSTRUCTION_SET_METADATA_BYTES = 4;
const INSTRUCTION_REVEAL_ENTRIES = 5;
const INSTRUCTION_ADD_WHITELIST_ENTRIES = 6;
const INSTRUCTION_DELETE_WHITELIST = 7;
const INSTRUCTION_WITHDRAW_EXCESS = 8;
const INSTRUCTION_BUY = 9;
const INSTRUCTION_REFUND = 10;
const INSTRUCTION_BID = 11;
const INSTRUCTION_CLAIM_LOSING = 12;
const INSTRUCTION_STAKE = 13;
const INSTRUCTION_DESTAKE = 14;
const INSTRUCTION_THAW = 15;
const INSTRUCTION_HARVEST = 16;
const INSTRUCTION_LEVEL_UP = 17;
const INSTRUCTION_SETTLE_WINNING_BID = 18;
const INSTRUCTION_DELEGATE = 19;
const INSTRUCTION_UPDATE_EPOCHS_STAKED = 20;
const INSTRUCTION_REAUTHORIZE = 21;

// Data type code of the authority account's data (DataType_Authority in program/inc/data_type.h)
const DATA_TYPE_AUTHORITY = 6;

// The types of account that the admin may withdraw excess lamports from with make_withdraw_excess_instruction().  The
// "code" of each is the value that the program knows the type by (WithdrawExcessAccountType in
// program/admin/admin_withdraw_excess.c).
const WithdrawExcessAccountType = Object.freeze(
{
    // The program state account
    ProgramState  : Object.freeze({ code : 0, name : "ProgramState" }),
    // The authority account.  The purchase prices of mysteries that have not yet been revealed or refunded are held
    // there in escrow, and are never withdrawn.
    Authority     : Object.freeze({ code : 1, name : "Authority" }),
    // A block account
    Block         : Object.freeze({ code : 2, name : "Block" }),
    // An entry account
    Entry         : Object.freeze({ code : 3, name : "Entry" }),
    // The whitelist account of a block
    Whitelist     : Object.freeze({ code : 4, name : "Whitelist" })
});


// Address derivation -------------------------------------------------------------------------------------------------


function get_metaplex_metadata_address(mint_address)
{
    return find_pda([ utf8_to_bytes("metadata"),
                      address_to_bytes(g_metaplex_program_address),
                      address_to_bytes(mint_address) ],
                    g_metaplex_program_address)[0];
}


function get_block_address(group_number, block_number)
{
    return find_pda([ [ 10 ],
                      u32_to_le_bytes(group_number),
                      u32_to_le_bytes(block_number) ],
                    g_self_program_address)[0];
}


function get_whitelist_address(block_address)
{
    return find_pda([ [ 9 ],
                      address_to_bytes(block_address) ],
                    g_self_program_address)[0];
}


function get_entry_mint_address(block_address, entry_index)
{
    return find_pda([ [ 4 ],
                      address_to_bytes(block_address),
                      u16_to_le_bytes(entry_index) ],
                    g_self_program_address)[0];
}


function get_entry_address(entry_mint_address)
{
    return find_pda([ [ 11 ],
                      address_to_bytes(entry_mint_address) ],
                    g_self_program_address)[0];
}


// Returns the address of an entry's own token account, which holds the entry's token from when the entry is added to
// its block until the entry is bought or its auction is settled
function get_entry_token_address(entry_mint_address)
{
    return find_pda([ [ 5 ],
                      address_to_bytes(entry_mint_address) ],
                    g_self_program_address)[0];
}


function get_associated_token_address(token_owner_address, token_mint_address)
{
    return find_pda([ address_to_bytes(token_owner_address),
                      address_to_bytes(g_spl_token_program_address),
                      address_to_bytes(token_mint_address) ],
                    g_spl_associated_token_program_address)[0];
}


function get_bid_marker_token_address(entry_mint_address, bidder_address)
{
    return find_pda([ [ 8 ],
                      address_to_bytes(entry_mint_address),
                      address_to_bytes(bidder_address) ],
                    g_self_program_address)[0];
}


function get_bid_address(bid_marker_token_address)
{
    return find_pda([ [ 6 ],
                      address_to_bytes(bid_marker_token_address) ],
                    g_self_program_address)[0];
}


// Returns the address of an entry: its address property if it has one (an Entry from shinobi_immortals.js does), or
// otherwise the address derived from its mint (as for an entry returned by decode_entry)
function entry_address_of(entry)
{
    return (entry.address != null) ? entry.address : get_entry_address(entry.mint_address);
}


// Returns the address of an entry's block: that of its block property if it has one (an Entry from
// shinobi_immortals.js does), or otherwise the address derived from its group and block numbers (as for an entry
// returned by decode_entry)
function block_address_of(entry)
{
    return ((entry.block != null) && (entry.block.address != null)) ?
        entry.block.address : get_block_address(entry.group_number, entry.block_number);
}


// Account data decoding ----------------------------------------------------------------------------------------------


// Decodes the data of the program state account (ProgramState in program/inc/program_state.h).  Returns null if the
// data is not long enough to be a program state; otherwise returns { admin_address : string }.
function decode_program_state(data)
{
    if ((data == null) || (data.length < 36)) {
        return null;
    }

    return { admin_address : buffer_address(data, 4) };
}


// Decodes the data of the authority account (Authority in program/inc/authority.h): a 4 byte data type, 4 bytes of
// padding, and the 8 byte escrow total.  Returns null if the data is not that of an authority account; otherwise
// returns { mystery_escrow_lamports : BigInt, the total purchase price of all mysteries that have been bought and not
// yet revealed or refunded }.
function decode_authority(data)
{
    if ((data == null) || (data.length != 16) || (buffer_le_u32(data, 0) != DATA_TYPE_AUTHORITY)) {
        return null;
    }

    return { mystery_escrow_lamports : buffer_le_u64(data, 8) };
}


// Decodes the data of a block account (Block in program/inc/block.h), returning an object holding its fields
function decode_block(data)
{
    let block = { };

    block.group_number = buffer_le_u32(data, 8);
    block.block_number = buffer_le_u32(data, 12);
    block.total_entry_count = buffer_le_u16(data, 16);
    block.total_mystery_count = buffer_le_u16(data, 18);
    block.mystery_phase_duration = buffer_le_u32(data, 20);
    block.mystery_start_price_lamports = buffer_le_u64(data, 24);
    block.reveal_period_duration = buffer_le_u32(data, 32);
    block.minimum_price_lamports = buffer_le_u64(data, 40);
    // A one byte bool, followed by padding, which need not be zero (the program copies it from instruction data)
    block.has_auction = data[48];
    block.duration = buffer_le_u32(data, 52);
    block.non_auction_start_price_lamports = buffer_le_u64(data, 56);
    block.whitelist_duration = buffer_le_u32(data, 64);
    block.added_entries_count = buffer_le_u16(data, 72);
    // The time at which the block was completed (0 until then), which is also the beginning of the life span of each of
    // its entries, over which their Ki harvests are reduced
    block.block_start_timestamp = Number(buffer_le_s64(data, 80));
    block.mysteries_sold_count = buffer_le_u16(data, 88);
    block.mystery_phase_end_timestamp = Number(buffer_le_s64(data, 96));

    return block;
}


// Decodes the data of an entry account (Entry in program/inc/entry.h), returning an object holding its fields.  The
// object can be passed to the instruction building functions below as the entry.
function decode_entry(data)
{
    let entry = { };

    entry.group_number = buffer_le_u32(data, 36);
    entry.block_number = buffer_le_u32(data, 40);
    entry.entry_index = buffer_le_u16(data, 44);
    entry.mint_address = buffer_address(data, 46);
    entry.token_address = buffer_address(data, 78);
    entry.metaplex_metadata_address = buffer_address(data, 110);
    entry.minimum_price_lamports = buffer_le_u64(data, 144);
    entry.has_auction = data[152];
    entry.duration = buffer_le_u32(data, 156);
    entry.non_auction_start_price_lamports = buffer_le_u64(data, 160);
    entry.reveal_sha256 = buffer_sha256(data, 168);
    entry.reveal_timestamp = Number(buffer_le_s64(data, 200));
    entry.purchase_price_lamports = buffer_le_u64(data, 208);
    entry.refund_awarded = data[216];
    // Nonzero once the entry's metadata has been reauthorized to another program; the entry is then retired
    entry.reauthorized = data[217];
    entry.auction_highest_bid_lamports = buffer_le_u64(data, 224);
    entry.auction_winning_bid_address = buffer_address(data, 232);
    entry.owned_stake_account = buffer_address(data, 264);
    entry.owned_last_ki_harvest_stake_account_lamports = buffer_le_u64(data, 296);
    entry.level = data[304];
    entry.most_recent_ki_applied_window = buffer_le_u64(data, 3160);
    entry.ki_applied_this_window = buffer_le_u64(data, 3168);
    entry.total_level_credits = buffer_le_u64(data, 3176);
    // The number of whole epochs that the entry has been staked with stake active at Shinobi Systems, as last
    // counted by the program, and the epoch up to which it has been counted (see program/inc/entry.h)
    entry.epochs_staked = buffer_le_u64(data, 3184);
    entry.epochs_staked_counted_to_epoch = buffer_le_u64(data, 3192);

    entry.metadata_level_1_credits = buffer_le_u32(data, 308);
    entry.metadata_random = [ buffer_le_u32(data, 312),
                             buffer_le_u32(data, 316),
                             buffer_le_u32(data, 320),
                             buffer_le_u32(data, 324),
                             buffer_le_u32(data, 328),
                             buffer_le_u32(data, 332),
                             buffer_le_u32(data, 336),
                             buffer_le_u32(data, 340),
                             buffer_le_u32(data, 344),
                             buffer_le_u32(data, 348),
                             buffer_le_u32(data, 352),
                             buffer_le_u32(data, 356),
                             buffer_le_u32(data, 360),
                             buffer_le_u32(data, 364),
                             buffer_le_u32(data, 368),
                             buffer_le_u32(data, 372) ];
    entry.level_metadata = [ ];

    for (let i = 0; i < 9; i += 1) {
        entry.level_metadata[i] = {
            // form is a uint32_t in LevelMetadata (program/inc/entry.h)
            form : buffer_le_u32(data, 376 + (i * 308)),
            skill : data[380 + (i * 308)],
            ki_factor : buffer_le_u32(data, 384 + (i * 308)),
            name : buffer_string(data, 388 + (i * 308), 32),
            title : buffer_string(data, 420 + (i * 308), 32),
            uri : buffer_string(data, 452 + (i * 308), 200),
            uri_contents_sha256 : buffer_sha256(data, 652 + (i * 308))
        };
    }

    // Expected annual stake reward rate in basis points, which calibrates the level credits curve
    entry.metadata_expected_annual_reward_bps = buffer_le_u32(data, 3148);

    // The fraction, of UINT32_MAX, by which Ki harvests are reduced for each day of the entry's life span
    entry.metadata_ki_harvest_reduction_per_day = buffer_le_u32(data, 3152);

    return entry;
}


// Calculations -----------------------------------------------------------------------------------------------------

// The unit in which Ki harvest reductions and retentions are expressed: each is a fraction of UINT32_MAX, so this
// value is one (all of a harvest).  KI_HARVEST_RETENTION_ONE in program/util/util_calculations.c.
const KI_HARVEST_RETENTION_ONE = 4294967295n;


// Mirrors compute_ki_harvest in program/util/util_calculations.c: returns as a BigInt the whole Ki earned by
// effective_earnings_lamports of effective stake earnings at ki_factor Ki per SOL, rounded down.  The program computes
// it in two parts to avoid overflow; with BigInts that is unnecessary, but it is done the same way so that the
// correspondence is plain.
function compute_ki_harvest(effective_earnings_lamports, ki_factor)
{
    let earnings = BigInt(effective_earnings_lamports);
    let factor = BigInt(ki_factor);
    let whole_sol = earnings / 1000000000n;
    let remaining_lamports = earnings % 1000000000n;
    return (whole_sol * factor) + ((remaining_lamports * factor) / 1000000000n);
}


// Mirrors compute_retained_ki_harvest in program/util/util_calculations.c: returns as a BigInt the whole Ki earned by
// effective_earnings_lamports of effective stake earnings at ki_factor Ki per SOL, after reduction to retention (a
// fraction of UINT32_MAX, as returned by compute_ki_harvest_retention).  The reduction is applied to the earnings
// before they are converted to Ki, rounding down (as the program's multiply_fraction does), so that only the final
// result is rounded to whole Ki.
function compute_retained_ki_harvest(effective_earnings_lamports, ki_factor, retention)
{
    retention = BigInt(retention);
    if (retention > KI_HARVEST_RETENTION_ONE) {
        retention = KI_HARVEST_RETENTION_ONE;
    }

    let retained_earnings_lamports = (BigInt(effective_earnings_lamports) * retention) / KI_HARVEST_RETENTION_ONE;

    return compute_ki_harvest(retained_earnings_lamports, ki_factor);
}


// Mirrors compute_ki_harvest_retention in program/util/util_calculations.c: returns as a BigInt the fraction, of
// UINT32_MAX, of a Ki harvest that is retained by an entry whose life span is lifespan_days whole days (a BigInt)
// and whose harvests are reduced by reduction_per_day (a fraction of UINT32_MAX) per day.  It is computed by
// repeated squaring, rounding down each product to a whole fraction of UINT32_MAX exactly as the program does, so
// that the result is identical.
function compute_ki_harvest_retention(reduction_per_day, lifespan_days)
{
    const ONE = KI_HARVEST_RETENTION_ONE;
    let base = ONE - BigInt(reduction_per_day);
    let result = ONE;
    lifespan_days = BigInt(lifespan_days);
    while (lifespan_days > 0n) {
        if (lifespan_days & 1n) {
            result = (result * base) / ONE;
        }
        if (result == 0n) {
            break;
        }
        base = (base * base) / ONE;
        lifespan_days >>= 1n;
    }
    return result;
}


// Mirrors compute_effective_stake_earnings in program/util/util_calculations.c, in BigInt lamports.  With
// K = EFFECTIVE_STAKE_CEILING_SOL (50 SOL) and x the stake, earnings are scaled by K(K + x) / (K(K + x) + x^2).
// The program computes x and K in units of 100 lamports, made ten times larger for as long as x is more than
// 2,000,000,000 units, and computes the floor of the scaled earnings exactly; this does the same.
function compute_effective_stake_earnings(earnings_lamports, stake_lamports)
{
    let stake_units = stake_lamports / 100n;
    let ceiling_units = 50n * (1000000000n / 100n);
    while (stake_units > 2000000000n) {
        stake_units /= 10n;
        ceiling_units /= 10n;
    }

    let numerator = ceiling_units * (ceiling_units + stake_units);
    let denominator = numerator + (stake_units * stake_units);

    return (earnings_lamports * numerator) / denominator;
}


// Mirrors compute_price in program/util/util_calculations.c, in BigInt lamports.  With T = total_seconds,
// t = seconds_elapsed and k = PRICE_CURVE_STEEPNESS (1069), before the end of the sale the price is
//   end_price + floor(floor((start_price - end_price) * T / (kt + T)) * k(T - t) / ((k + 1)T)),
// which is the curve 1/(kx + 1) - 1/(k + 1), x = t/T, scaled between the start and end prices: it falls to 1/10
// of the way from end_price to start_price after 1/120 of the sale (2 hours of a 10 day sale), then keeps falling
// more and more slowly to end_price at the end.  BigInt division of non-negative values rounds down, as the
// program's does.
function compute_price(total_seconds, start_price, end_price, seconds_elapsed)
{
    const PRICE_CURVE_STEEPNESS = 1069n;

    if (seconds_elapsed >= total_seconds) {
        return end_price;
    }

    let added = ((start_price - end_price) * total_seconds) /
        ((PRICE_CURVE_STEEPNESS * seconds_elapsed) + total_seconds);

    added = (added * PRICE_CURVE_STEEPNESS * (total_seconds - seconds_elapsed)) /
        ((PRICE_CURVE_STEEPNESS + 1n) * total_seconds);

    return end_price + added;
}


// Mirrors compute_minimum_bid in program/util/util_calculations.c: the first bid must be at least the initial
// minimum; every later bid must exceed the current highest bid by at least MINIMUM_BID_INCREMENT_BPS of it,
// rounded up and at least 1 lamport, saturating at the largest u64 value.  Returns 0 if no bid is possible.
function compute_minimum_bid(initial_minimum_bid, current_max_bid)
{
    const U64_MAX = 18446744073709551615n;
    // MINIMUM_BID_INCREMENT_BPS: 500 basis points, which is 5%
    const MINIMUM_BID_INCREMENT_BPS = 500n;

    if (current_max_bid == U64_MAX) {
        return 0n;
    }

    if (current_max_bid < initial_minimum_bid) {
        return initial_minimum_bid;
    }

    let increment = ((current_max_bid * MINIMUM_BID_INCREMENT_BPS) + 9999n) / 10000n;
    if (increment == 0n) {
        increment = 1n;
    }

    let result = current_max_bid + increment;

    return (result > U64_MAX) ? U64_MAX : result;
}


// Returns the excess of an account that holds lamports and is required to hold required_lamports (both BigInt): the
// amount that the WithdrawExcess instruction would withdraw from it, which is zero if it holds no more than is
// required
function excess_of(lamports, required_lamports)
{
    return (lamports > required_lamports) ? (lamports - required_lamports) : 0n;
}


// Instruction building -----------------------------------------------------------------------------------------------
//
// Each of these returns an instruction, as described under "Instructions" above.  Where an entry is taken, it may be
// an Entry from shinobi_immortals.js or an entry returned by decode_entry.  Addresses are base58 strings.  Lamport
// and Ki amounts may be BigInts, numbers, or decimal strings (use a BigInt or a string for values that do not fit in
// 53 bits).


// Superuser instructions ---------------------------------------------------------------------------------------------
//
// These can only be performed by the superuser, whose address is compiled into the program.


// Accounts and data here must match those of scripts/super_initialize_tx.sh.  Initializes the program: creates the
// program state account (recording admin_address as the admin), the authority account, and the Ki and bid marker
// mints and their metadata, all paid for by the superuser.  This can only be done once.
function make_initialize_instruction(superuser_address, admin_address)
{
    let accounts = [
        // The superuser, who must sign, and who pays for the accounts that are created
        account_meta(superuser_address, true, true),
        // The program state account, which is created
        account_meta(g_program_state_address, false, true),
        // The program authority account, which is created
        account_meta(g_program_authority_address, false, true),
        // The Ki mint, which is created
        account_meta(g_ki_mint_address, false, true),
        // The Ki mint's Metaplex metadata, which is created
        account_meta(g_ki_metadata_address, false, true),
        // The bid marker mint, which is created
        account_meta(g_bid_marker_mint_address, false, true),
        // The bid marker mint's Metaplex metadata, which is created
        account_meta(g_bid_marker_metadata_address, false, true),
        // Rent sysvar
        account_meta(g_rent_sysvar_address, false, false),
        // The system program
        account_meta(g_system_program_address, false, false),
        // The SPL Token program
        account_meta(g_spl_token_program_address, false, false),
        // The Metaplex program
        account_meta(g_metaplex_program_address, false, false)
    ];

    // The instruction data is the instruction code and then the admin's address (InitializeData in
    // program/super/super_initialize.c)
    let data = [ INSTRUCTION_INITIALIZE, ...address_to_bytes(admin_address) ];

    return make_instruction(accounts, data);
}


// Accounts and data here must match those of scripts/super_set_admin_tx.sh.  Changes the admin to new_admin_address.
function make_set_admin_instruction(superuser_address, new_admin_address)
{
    let accounts = [
        // The superuser, who must sign
        account_meta(superuser_address, true, false),
        // The program state account, which records the admin
        account_meta(g_program_state_address, false, true)
    ];

    // The instruction data is the instruction code and then the new admin's address (UpdateAdminData in
    // program/super/super_set_admin.c)
    let data = [ INSTRUCTION_SET_ADMIN, ...address_to_bytes(new_admin_address) ];

    return make_instruction(accounts, data);
}


// Admin instructions -------------------------------------------------------------------------------------------------
//
// These can only be performed by the admin, whose address is held in the program state account (see
// decode_program_state).  Blocks are identified by their group number and block number, and entries by those and their
// index within the block, as in the scripts.  Where a funding_address is taken, it is the account that pays for the
// accounts that are created, and must also sign; if it is not given, the admin pays.


// Accounts and data here must match those of scripts/admin_create_block_tx.sh.  Creates a block.  config holds the
// block's configuration, with the fields of BlockConfiguration in program/inc/block.h (see there for their meanings):
//
// {
//     group_number : the group number of the block,
//     block_number : the block number of the block within its group,
//     total_entry_count : the number of entries in the block,
//     total_mystery_count : the number of entries which may be bought as mysteries,
//     mystery_phase_duration : seconds,
//     mystery_start_price_lamports : lamports,
//     reveal_period_duration : seconds,
//     minimum_price_lamports : lamports,
//     has_auction : true or false,
//     duration : seconds,
//     final_start_price_lamports : lamports,
//     whitelist_duration : seconds; if nonzero, the block's whitelist must already exist and be non-empty
// }
function make_create_block_instruction(admin_address, config, funding_address)
{
    if (funding_address == null) {
        funding_address = admin_address;
    }

    let block_address = get_block_address(config.group_number, config.block_number);

    let accounts = [
        // The program state account, which identifies the admin
        account_meta(g_program_state_address, false, false),
        // The admin, who must sign
        account_meta(admin_address, true, false),
        // The funding account, which pays for the block account
        account_meta(funding_address, true, true),
        // The block account, which is created
        account_meta(block_address, false, true),
        // The system program
        account_meta(g_system_program_address, false, false),
        // The block's whitelist, which is only used if the block uses a whitelist (whitelist_duration is nonzero)
        account_meta(get_whitelist_address(block_address), false, false)
    ];

    // The instruction data is the instruction code, 7 bytes of padding (CreateBlockData in
    // program/admin/admin_create_block.c aligns the configuration to 8 bytes), and then the BlockConfiguration, laid
    // out with C alignment as commented
    let data = [
        INSTRUCTION_CREATE_BLOCK, 0, 0, 0, 0, 0, 0, 0,
        // Offset 0 of the configuration
        ...u32_to_le_bytes(config.group_number),
        // Offset 4
        ...u32_to_le_bytes(config.block_number),
        // Offset 8
        ...u16_to_le_bytes(config.total_entry_count),
        // Offset 10
        ...u16_to_le_bytes(config.total_mystery_count),
        // Offset 12
        ...u32_to_le_bytes(config.mystery_phase_duration),
        // Offset 16
        ...u64_to_le_bytes(config.mystery_start_price_lamports),
        // Offset 24, followed by 4 bytes of padding to align the next u64
        ...u32_to_le_bytes(config.reveal_period_duration), 0, 0, 0, 0,
        // Offset 32
        ...u64_to_le_bytes(config.minimum_price_lamports),
        // Offset 40, a bool followed by 3 bytes of padding to align the next u32
        (config.has_auction ? 1 : 0), 0, 0, 0,
        // Offset 44
        ...u32_to_le_bytes(config.duration),
        // Offset 48
        ...u64_to_le_bytes(config.final_start_price_lamports),
        // Offset 56, followed by 4 bytes of padding to make the structure size a multiple of 8
        ...u32_to_le_bytes(config.whitelist_duration), 0, 0, 0, 0
    ];

    return make_instruction(accounts, data);
}


// Accounts and data here must match those of scripts/admin_add_entries_to_block_tx.sh.  Adds entries to a block,
// starting at index first_entry_index, one for each element of entry_sha256s.  Each element of entry_sha256s is the
// SHA-256 of the entry's metadata and salt, as a 64 character hex string or as 32 bytes; it is checked when the entry
// is revealed.  metaplex_metadata_uri is the initial uri of each entry's Metaplex metadata, at most 200 bytes of UTF-8.
// second_creator_address is an additional creator to put in each entry's Metaplex metadata, or null for none.
//
// Each entry adds 4 accounts and 32 bytes to the transaction, which limits how many can be added at once.
function make_add_entries_to_block_instruction(admin_address, group_number, block_number, metaplex_metadata_uri,
                                               second_creator_address, first_entry_index, entry_sha256s,
                                               funding_address)
{
    if (funding_address == null) {
        funding_address = admin_address;
    }

    // The program treats a second creator of all zeroes (which is the system program address) as none
    if (second_creator_address == null) {
        second_creator_address = g_system_program_address;
    }

    let block_address = get_block_address(group_number, block_number);

    let accounts = [
        // The program state account, which identifies the admin
        account_meta(g_program_state_address, false, false),
        // The admin, who must sign
        account_meta(admin_address, true, false),
        // The funding account, which pays for the accounts of each entry
        account_meta(funding_address, true, true),
        // The block
        account_meta(block_address, false, true),
        // The program authority
        account_meta(g_program_authority_address, false, false),
        // The system program
        account_meta(g_system_program_address, false, false),
        // The SPL Token program
        account_meta(g_spl_token_program_address, false, false),
        // The Metaplex program
        account_meta(g_metaplex_program_address, false, false),
        // Rent sysvar
        account_meta(g_rent_sysvar_address, false, false)
    ];

    let sha256_bytes = [ ];

    for (let i = 0; i < entry_sha256s.length; i++) {
        let mint_address = get_entry_mint_address(block_address, first_entry_index + i);
        // The entry account, which is created
        accounts.push(account_meta(get_entry_address(mint_address), false, true));
        // The entry's mint, which is created
        accounts.push(account_meta(mint_address, false, true));
        // The entry's own token account, which is created and receives the entry's token
        accounts.push(account_meta(get_entry_token_address(mint_address), false, true));
        // The entry's Metaplex metadata, which is created
        accounts.push(account_meta(get_metaplex_metadata_address(mint_address), false, true));

        sha256_bytes.push(...sha256_to_bytes(entry_sha256s[i]));
    }

    // The instruction data is laid out as AddEntriesToBlockData in program/admin/admin_add_entries_to_block.c: the
    // instruction code, the 200 byte zero padded uri, the second creator, a byte of padding to align the u16 that
    // follows, the index of the first entry, and then the SHA-256 of each entry
    let data = [ INSTRUCTION_ADD_ENTRIES_TO_BLOCK,
                                    ...c_string_to_bytes(metaplex_metadata_uri, 200),
                                    ...address_to_bytes(second_creator_address),
                                    0,
                                    ...u16_to_le_bytes(first_entry_index),
                                    ...sha256_bytes ];

    return make_instruction(accounts, data);
}


// Accounts and data here must match those of scripts/admin_set_metadata_bytes_tx.sh.  Sets bytes of an entry's
// metadata (EntryMetadata in program/inc/entry.h), starting at byte start_index of the metadata.  bytes is an array,
// Uint8Array, or Buffer of the bytes to set.  This is only possible before the entry is revealed, and the bytes must
// be such that the metadata, once complete, matches the SHA-256 that the entry was added with.
function make_set_metadata_bytes_instruction(admin_address, group_number, block_number, entry_index, start_index, bytes)
{
    let block_address = get_block_address(group_number, block_number);

    let entry_address = get_entry_address(get_entry_mint_address(block_address, entry_index));

    let accounts = [
        // The program state account, which identifies the admin
        account_meta(g_program_state_address, false, false),
        // The admin, who must sign
        account_meta(admin_address, true, false),
        // The block
        account_meta(block_address, false, false),
        // The entry, whose metadata is set
        account_meta(entry_address, false, true)
    ];

    // The instruction data is laid out as SetMetadataBytesData in program/admin/admin_set_metadata_bytes.c: the
    // instruction code, a byte of padding to align the u16s that follow, the start index, the number of bytes, and
    // then the bytes
    let data = [ INSTRUCTION_SET_METADATA_BYTES, 0,
                                    ...u16_to_le_bytes(start_index),
                                    ...u16_to_le_bytes(bytes.length),
                                    ...bytes ];

    return make_instruction(accounts, data);
}


// Accounts and data here must match those of scripts/admin_reveal_entries_tx.sh.  Reveals entries of a block,
// starting at index first_entry_index, one for each element of salts.  Each element of salts is the salt (a u64, as a
// BigInt, number, or decimal string) that was hashed with the entry's metadata to produce the SHA-256 that the entry
// was added with.  The purchase prices of revealed entries that were bought as mysteries, which the program authority
// held in escrow until now, go to the admin.
//
// Each entry adds 2 accounts and 8 bytes to the transaction, which limits how many can be revealed at once.
function make_reveal_entries_instruction(admin_address, group_number, block_number, first_entry_index, salts)
{
    let block_address = get_block_address(group_number, block_number);

    let accounts = [
        // The program state account, which identifies the admin
        account_meta(g_program_state_address, false, false),
        // The admin, who must sign, and who receives the escrowed purchase prices of revealed mysteries
        account_meta(admin_address, true, true),
        // The block
        account_meta(block_address, false, true),
        // The program authority, which holds the purchase prices of mysteries in escrow until they are revealed
        account_meta(g_program_authority_address, false, true),
        // The system program
        account_meta(g_system_program_address, false, false),
        // The Metaplex program
        account_meta(g_metaplex_program_address, false, false)
    ];

    let salt_bytes = [ ];

    for (let i = 0; i < salts.length; i++) {
        let mint_address = get_entry_mint_address(block_address, first_entry_index + i);
        // The entry, which is revealed
        accounts.push(account_meta(get_entry_address(mint_address), false, true));
        // The entry's Metaplex metadata, which is updated with the revealed metadata
        accounts.push(account_meta(get_metaplex_metadata_address(mint_address), false, true));

        salt_bytes.push(...u64_to_le_bytes(salts[i]));
    }

    // The instruction data is laid out as RevealEntriesData in program/admin/admin_reveal_entries.c: the instruction
    // code, a byte of padding to align the u16 that follows, the index of the first entry, 4 bytes of padding to align
    // the u64s that follow, and then the salt of each entry
    let data = [ INSTRUCTION_REVEAL_ENTRIES, 0,
                                    ...u16_to_le_bytes(first_entry_index),
                                    0, 0, 0, 0,
                                    ...salt_bytes ];

    return make_instruction(accounts, data);
}


// The most addresses that one AddWhitelistEntries instruction can add (MAX_WHITELIST_ADD_ENTRIES in
// program/admin/admin_add_whitelist_entries.c)
const MAX_WHITELIST_ADD_ENTRIES = 28;


// Accounts and data here must match those of scripts/admin_add_whitelist_entries_tx.sh.  Adds addresses to the
// whitelist of a block, creating the whitelist if it does not exist yet.  This is only possible before the block is
// created.  addresses is an array of from 1 to MAX_WHITELIST_ADD_ENTRIES addresses; an address that appears more than
// once may buy that many entries.
function make_add_whitelist_entries_instruction(admin_address, group_number, block_number, addresses, funding_address)
{
    if (funding_address == null) {
        funding_address = admin_address;
    }

    if ((addresses.length == 0) || (addresses.length > MAX_WHITELIST_ADD_ENTRIES)) {
        throw new Error("From 1 to " + MAX_WHITELIST_ADD_ENTRIES + " whitelist addresses may be added at once");
    }

    let block_address = get_block_address(group_number, block_number);

    let accounts = [
        // The program state account, which identifies the admin
        account_meta(g_program_state_address, false, false),
        // The admin, who must sign
        account_meta(admin_address, true, false),
        // The funding account, which pays for the whitelist account if it has to be created
        account_meta(funding_address, true, true),
        // The block, which need not exist yet (and must not, for the whitelist to be added to)
        account_meta(block_address, false, false),
        // The block's whitelist
        account_meta(get_whitelist_address(block_address), false, true),
        // The system program
        account_meta(g_system_program_address, false, false)
    ];

    let address_bytes = [ ];

    for (let address of addresses) {
        address_bytes.push(...address_to_bytes(address));
    }

    // The instruction data is laid out as AddWhitelistEntriesData in program/admin/admin_add_whitelist_entries.c: the
    // instruction code, a byte of padding to align the u16 that follows, the number of addresses, and then the
    // addresses
    let data = [ INSTRUCTION_ADD_WHITELIST_ENTRIES, 0,
                                    ...u16_to_le_bytes(addresses.length),
                                    ...address_bytes ];

    return make_instruction(accounts, data);
}


// Accounts and data here must match those of scripts/admin_delete_whitelist_tx.sh.  Deletes the whitelist of a block,
// returning its lamports to the admin.  See the description of whitelist_duration in program/inc/block.h for when this
// is allowed.
function make_delete_whitelist_instruction(admin_address, group_number, block_number)
{
    let block_address = get_block_address(group_number, block_number);

    let accounts = [
        // The program state account, which identifies the admin
        account_meta(g_program_state_address, false, false),
        // The admin, who must sign, and who receives the whitelist's lamports
        account_meta(admin_address, true, true),
        // The block, which need not exist
        account_meta(block_address, false, false),
        // The block's whitelist, which is deleted
        account_meta(get_whitelist_address(block_address), false, true)
    ];

    // The instruction data is just the instruction code
    let data = [ INSTRUCTION_DELETE_WHITELIST ];

    return make_instruction(accounts, data);
}


// Accounts and data here must match those of scripts/admin_withdraw_excess_tx.sh
function make_withdraw_excess_instruction(account_type, account, wallet_address)
{
    // A Block or Entry may be passed in, or just an address
    let account_address = ((account != null) && (account.address != null)) ? account.address : account;

    // Find the address of the account to withdraw from
    let from_address;
    switch (account_type) {
    case WithdrawExcessAccountType.ProgramState:
        from_address = g_program_state_address;
        break;
    case WithdrawExcessAccountType.Authority:
        from_address = g_program_authority_address;
        break;
    case WithdrawExcessAccountType.Block:
    case WithdrawExcessAccountType.Entry:
        from_address = account_address;
        break;
    case WithdrawExcessAccountType.Whitelist:
        // The whitelist account is derived from the address of its block
        from_address = (account_address == null) ? null : get_whitelist_address(account_address);
        break;
    default:
        throw new Error("Invalid account type");
    }

    if (from_address == null) {
        throw new Error("No account to withdraw from was provided for account type " + account_type.name);
    }

    let accounts = [
        // The program state account, which identifies the admin
        account_meta(g_program_state_address, false, false),
        // The admin, who must sign and who receives the lamports
        account_meta(wallet_address, true, true),
        // The account to withdraw from
        account_meta(from_address, false, true)
    ];

    // The instruction data is the instruction code followed by the code of the account type
    let data = [ INSTRUCTION_WITHDRAW_EXCESS, account_type.code ];

    return make_instruction(accounts, data);
}


// User instructions --------------------------------------------------------------------------------------------------
//
// These are performed by users: buyers and bidders, and the owners of entries.


// Accounts and data here must match those of scripts/user_buy_tx.sh.  admin_address is the admin's address, which
// is held in the program state account (see decode_program_state).
function make_buy_instruction(entry, maximum_price_lamports, wallet_address, admin_address)
{
    let accounts = [
        // The funding account, which pays the purchase price
        account_meta(wallet_address, true, true),
        // The program state account, which identifies the admin
        account_meta(g_program_state_address, false, false),
        // The admin, who receives the purchase price of a non-mystery
        account_meta(admin_address, false, true),
        // The program authority, which holds the purchase price of a mystery in escrow
        account_meta(g_program_authority_address, false, true),
        // The block
        account_meta(block_address_of(entry), false, true),
        // The block's whitelist, whose entry for the funding account is used up if the block is in its whitelist
        // period
        account_meta(get_whitelist_address(block_address_of(entry)), false, true),
        // The entry
        account_meta(entry_address_of(entry), false, true),
        // The entry's own token account, which holds the token until now and is closed
        account_meta(entry.token_address, false, true),
        // The entry's mint
        account_meta(entry.mint_address, false, false),
        // The wallet's associated token account for the entry's mint, which receives the token, and is created if
        // it does not exist yet
        account_meta(get_associated_token_address(wallet_address, entry.mint_address), false, true),
        // The owner of that token account
        account_meta(wallet_address, false, false),
        // This program
        account_meta(g_self_program_address, false, false),
        // The SPL Token program
        account_meta(g_spl_token_program_address, false, false),
        // The Associated Token Account program
        account_meta(g_spl_associated_token_program_address, false, false),
        // The system program
        account_meta(g_system_program_address, false, false)
    ];

    // The instruction data is the instruction code, 7 bytes of padding (the program's BuyData struct aligns its
    // u64 to 8 bytes), and then the maximum price in lamports
    let data = [ INSTRUCTION_BUY, 0, 0, 0, 0, 0, 0, 0,
                                    ...u64_to_le_bytes(maximum_price_lamports) ];

    return make_instruction(accounts, data);
}


// Accounts and data here must match those of scripts/user_refund_tx.sh.  token_address is the token account that
// holds the entry's token; if it is not given, the wallet's associated token account for the entry's mint is used.
function make_refund_instruction(entry, wallet_address, token_address)
{
    if (token_address == null) {
        token_address = get_associated_token_address(wallet_address, entry.mint_address);
    }

    let accounts = [
        // The token owner, who must sign
        account_meta(wallet_address, true, false),
        // The block
        account_meta(block_address_of(entry), false, false),
        // The entry
        account_meta(entry_address_of(entry), false, true),
        // The program authority, which holds the purchase price in escrow and pays the refund
        account_meta(g_program_authority_address, false, true),
        // The token account holding the entry's token
        account_meta(token_address, false, false),
        // The account that receives the refund
        account_meta(wallet_address, false, true)
    ];

    // The instruction data is just the instruction code
    let data = [ INSTRUCTION_REFUND ];

    return make_instruction(accounts, data);
}


// Accounts and data here must match those of scripts/user_bid_tx.sh
function make_bid_instruction(entry, minimum_bid_lamports, maximum_bid_lamports, wallet_address)
{
    let bid_marker_token_address = get_bid_marker_token_address(entry.mint_address, wallet_address);

    let accounts = [
        // The bidding account, which pays the bid
        account_meta(wallet_address, true, true),
        // The entry
        account_meta(entry_address_of(entry), false, true),
        // The bid marker mint
        account_meta(g_bid_marker_mint_address, false, true),
        // The bidder's bid marker token account for the entry
        account_meta(bid_marker_token_address, false, true),
        // The bid account, which holds the bid
        account_meta(get_bid_address(bid_marker_token_address), false, true),
        // The program authority
        account_meta(g_program_authority_address, false, false),
        // The system program
        account_meta(g_system_program_address, false, false),
        // This program
        account_meta(g_self_program_address, false, false),
        // The SPL Token program
        account_meta(g_spl_token_program_address, false, false)
    ];

    // The instruction data is the instruction code, 7 bytes of padding (the program's BidData struct aligns its
    // u64s to 8 bytes), and then the minimum and maximum bids in lamports
    let data = [ INSTRUCTION_BID, 0, 0, 0, 0, 0, 0, 0,
                                    ...u64_to_le_bytes(minimum_bid_lamports),
                                    ...u64_to_le_bytes(maximum_bid_lamports) ];

    return make_instruction(accounts, data);
}


// Accounts and data here must match those of scripts/user_claim_losing_tx.sh.  The bid marker is reclaimed too
// (its tokens are burned and its token account closed), unless keep_bid_marker is true; keep_bid_marker is needed
// only if the bidder has already burned or closed their bid marker, which would make reclaiming it fail.
function make_claim_losing_instruction(entry, wallet_address, keep_bid_marker)
{
    let bid_marker_token_address = get_bid_marker_token_address(entry.mint_address, wallet_address);

    let accounts = [
        // The bidding account, which receives the bid back
        account_meta(wallet_address, true, true),
        // The entry
        account_meta(entry_address_of(entry), false, false),
        // The bid account, which is closed
        account_meta(get_bid_address(bid_marker_token_address), false, true)
    ];

    if (!keep_bid_marker) {
        // The bid marker mint
        accounts.push(account_meta(g_bid_marker_mint_address, false, true));
        // The bidder's bid marker token account for the entry
        accounts.push(account_meta(bid_marker_token_address, false, true));
        // The program authority
        accounts.push(account_meta(g_program_authority_address, false, false));
        // The SPL Token program
        accounts.push(account_meta(g_spl_token_program_address, false, false));
    }

    // The instruction data is just the instruction code
    let data = [ INSTRUCTION_CLAIM_LOSING ];

    return make_instruction(accounts, data);
}


// Accounts and data here must match those of scripts/user_stake_tx.sh.  The wallet must be both the owner of the
// entry's token (held in its associated token account) and the withdraw authority of the stake account.
function make_stake_instruction(entry, stake_address, wallet_address)
{
    let accounts = [
        // The block
        account_meta(block_address_of(entry), false, false),
        // The entry
        account_meta(entry_address_of(entry), false, true),
        // The token owner
        account_meta(wallet_address, true, false),
        // The token account holding the entry's token, which is frozen for as long as the entry is staked
        account_meta(get_associated_token_address(wallet_address, entry.mint_address), false, true),
        // The stake account
        account_meta(stake_address, false, true),
        // The withdraw authority of the stake account
        account_meta(wallet_address, true, false),
        // The Shinobi Systems vote account
        account_meta(g_shinobi_systems_vote_address, false, false),
        // The program authority, which becomes the stake account's authority
        account_meta(g_program_authority_address, false, false),
        // Clock sysvar
        account_meta(g_clock_sysvar_address, false, false),
        // The stake program
        account_meta(g_stake_program_address, false, false),
        // The stake config account, required by the stake program
        account_meta(g_stake_config_address, false, false),
        // Stake history sysvar, required by the stake program
        account_meta(g_stake_history_sysvar_address, false, false),
        // The entry's mint, needed to freeze the token account
        account_meta(entry.mint_address, false, false),
        // The SPL Token program
        account_meta(g_spl_token_program_address, false, false)
    ];

    // The instruction data is just the instruction code
    let data = [ INSTRUCTION_STAKE ];

    return make_instruction(accounts, data);
}


// Accounts and data here must match those of scripts/user_destake_tx.sh
function make_destake_instruction(entry, wallet_address, no_ki_harvest, no_thaw)
{
    let accounts = [
        // The funding account, which pays for the Ki destination account if it does not exist yet
        account_meta(wallet_address, true, true),
        // The block
        account_meta(block_address_of(entry), false, false),
        // The entry
        account_meta(entry_address_of(entry), false, true),
        // The token owner, who receives the stake account
        account_meta(wallet_address, true, false),
        // The token account holding the entry's token, which is thawed unless no_thaw
        account_meta(get_associated_token_address(wallet_address, entry.mint_address), false, true),
        // The entry's stake account
        account_meta(entry.owned_stake_account, false, true),
        // The Ki destination account, which receives harvested Ki
        account_meta(get_associated_token_address(wallet_address, g_ki_mint_address), false, true),
        // The owner of the Ki destination account
        account_meta(wallet_address, false, false),
        // The Ki mint
        account_meta(g_ki_mint_address, false, true),
        // The program authority
        account_meta(g_program_authority_address, false, false),
        // Clock sysvar
        account_meta(g_clock_sysvar_address, false, false),
        // The system program
        account_meta(g_system_program_address, false, false),
        // The stake program
        account_meta(g_stake_program_address, false, false),
        // The SPL Token program
        account_meta(g_spl_token_program_address, false, false),
        // The Associated Token Account program
        account_meta(g_spl_associated_token_program_address, false, false),
        // The entry's mint, which is needed to thaw the token account unless no_thaw
        account_meta(entry.mint_address, false, false)
    ];

    // The instruction data is the instruction code, then the no_ki_harvest flag (1 = do not harvest Ki), then the
    // no_thaw flag (1 = leave the token account frozen)
    let data = [ INSTRUCTION_DESTAKE, (no_ki_harvest ? 1 : 0), (no_thaw ? 1 : 0) ];

    return make_instruction(accounts, data);
}


// Accounts and data here must match those of scripts/user_thaw_tx.sh
function make_thaw_instruction(entry, wallet_address)
{
    let accounts = [
        // The block
        account_meta(block_address_of(entry), false, false),
        // The entry
        account_meta(entry_address_of(entry), false, false),
        // The token owner
        account_meta(wallet_address, true, false),
        // The token account holding the entry's token, which is thawed
        account_meta(get_associated_token_address(wallet_address, entry.mint_address), false, true),
        // The entry's mint
        account_meta(entry.mint_address, false, false),
        // The program authority, which is the freeze authority of the entry's mint
        account_meta(g_program_authority_address, false, false),
        // The SPL Token program
        account_meta(g_spl_token_program_address, false, false)
    ];

    // The instruction data is just the instruction code
    let data = [ INSTRUCTION_THAW ];

    return make_instruction(accounts, data);
}


// Accounts and data here must match those of scripts/user_harvest_tx.sh
function make_harvest_instruction(entry, wallet_address)
{
    let accounts = [
        // The funding account, which pays for the Ki destination account if it does not exist yet
        account_meta(wallet_address, true, true),
        // The entry
        account_meta(entry_address_of(entry), false, true),
        // The token owner
        account_meta(wallet_address, true, false),
        // The token account holding the entry's token
        account_meta(get_associated_token_address(wallet_address, entry.mint_address), false, false),
        // The entry's stake account
        account_meta(entry.owned_stake_account, false, false),
        // The Ki destination account, which receives harvested Ki
        account_meta(get_associated_token_address(wallet_address, g_ki_mint_address), false, true),
        // The owner of the Ki destination account
        account_meta(wallet_address, false, false),
        // The Ki mint
        account_meta(g_ki_mint_address, false, true),
        // The program authority
        account_meta(g_program_authority_address, false, false),
        // The system program
        account_meta(g_system_program_address, false, false),
        // The SPL Token program
        account_meta(g_spl_token_program_address, false, false),
        // The Associated Token Account program
        account_meta(g_spl_associated_token_program_address, false, false),
        // The entry's block, whose block_start_timestamp is the beginning of the entry's life span
        account_meta(block_address_of(entry), false, false)
    ];

    // The instruction data is just the instruction code
    let data = [ INSTRUCTION_HARVEST ];

    return make_instruction(accounts, data);
}


// Accounts and data here must match those of scripts/user_level_up_tx.sh.  ki_to_apply is the most whole Ki to
// burn; only as much of it as earns level credits is burned.  The Ki comes from the wallet's associated Ki token
// account, which LevelUp creates (paid for by the wallet) if it does not exist yet.
function make_level_up_instruction(entry, ki_to_apply, wallet_address)
{
    let accounts = [
        // The entry
        account_meta(entry_address_of(entry), false, true),
        // The token owner
        account_meta(wallet_address, true, false),
        // The token account holding the entry's token
        account_meta(get_associated_token_address(wallet_address, entry.mint_address), false, false),
        // The entry's Metaplex metadata, which is updated to the new level
        account_meta(entry.metaplex_metadata_address, false, true),
        // The Ki source account, from which Ki is burned
        account_meta(get_associated_token_address(wallet_address, g_ki_mint_address), false, true),
        // The owner of the Ki source account, which pays for that account if it has to be created
        account_meta(wallet_address, true, true),
        // The Ki mint
        account_meta(g_ki_mint_address, false, true),
        // The program authority
        account_meta(g_program_authority_address, false, false),
        // The SPL Token program
        account_meta(g_spl_token_program_address, false, false),
        // The Metaplex program
        account_meta(g_metaplex_program_address, false, false),
        // The entry's stake account; for an entry that is not staked this is the system program address, which is
        // the placeholder that LevelUp expects
        account_meta(entry.owned_stake_account, false, false),
        // The system program
        account_meta(g_system_program_address, false, false),
        // The Associated Token Account program
        account_meta(g_spl_associated_token_program_address, false, false),
        // The entry's block, whose block_start_timestamp is the beginning of the entry's life span; LevelUp only
        // uses it if the entry is staked
        account_meta(block_address_of(entry), false, false)
    ];

    // The instruction data is the instruction code, 7 bytes of padding (the program's LevelUpData struct aligns
    // its u64 to 8 bytes), and then the most Ki to apply
    let data = [ INSTRUCTION_LEVEL_UP, 0, 0, 0, 0, 0, 0, 0, ...u64_to_le_bytes(ki_to_apply) ];

    return make_instruction(accounts, data);
}


// Anyone instructions ------------------------------------------------------------------------------------------------
//
// These can be performed by any wallet (though SettleWinningBid only by the winning bidder until 30 days have
// passed since the end of the auction).


// Accounts and data here must match those of scripts/anyone_settle_winning_bid_tx.sh.  admin_address is the
// admin's address, which is held in the program state account (see decode_program_state).
//
// If the wallet is the bidder, the bid marker is reclaimed too, unless keep_bid_marker is true; keep_bid_marker
// is needed only if the bidder has already burned or closed their bid marker, which would make reclaiming it fail.
function make_settle_winning_bid_instruction(entry, bidder_address, admin_address, wallet_address, keep_bid_marker)
{
    // The bid account of the bidder for this entry
    let bid_address = get_bid_address(get_bid_marker_token_address(entry.mint_address, bidder_address));

    let accounts = [
        // The funding account, which pays for the bidder's associated token account if it does not exist yet
        account_meta(wallet_address, true, true),
        // The entry
        account_meta(entry_address_of(entry), false, true),
        // The winning bid account
        account_meta(bid_address, false, true),
        // The program state account, which identifies the admin
        account_meta(g_program_state_address, false, false),
        // The admin, who receives the winning bid
        account_meta(admin_address, false, true),
        // The entry's own token account, which holds the token until now and is closed
        account_meta(entry.token_address, false, true),
        // The entry's mint
        account_meta(entry.mint_address, false, false),
        // The program authority
        account_meta(g_program_authority_address, false, false),
        // The bidder's associated token account for the entry's mint, which receives the token
        account_meta(get_associated_token_address(bidder_address, entry.mint_address), false, true),
        // The bidder, who receives any lamports that the bid account holds beyond the winning bid
        account_meta(bidder_address, false, true),
        // The system program
        account_meta(g_system_program_address, false, false),
        // The SPL Token program
        account_meta(g_spl_token_program_address, false, false),
        // The Associated Token Account program
        account_meta(g_spl_associated_token_program_address, false, false)
    ];

    // When the bidder settles their own bid, also reclaim the bid marker, which has no purpose once the bid is
    // settled: two more accounts make the program burn the bidder's bid marker tokens for the entry and close the
    // bid marker token account, returning its lamports to the bidder.  Nobody else can do this, since it needs
    // the bidder's signature.
    if ((wallet_address == bidder_address) && !keep_bid_marker) {
        // The bid marker mint
        accounts.push(account_meta(g_bid_marker_mint_address, false, true));
        // The bidder's bid marker token account for the entry
        accounts.push(account_meta(get_bid_marker_token_address(entry.mint_address, bidder_address), false, true));
    }

    // The instruction data is just the instruction code
    let data = [ INSTRUCTION_SETTLE_WINNING_BID ];

    return make_instruction(accounts, data);
}


// Accounts and data here must match those of scripts/anyone_delegate_tx.sh
function make_delegate_instruction(entry)
{
    let accounts = [
        // The block
        account_meta(block_address_of(entry), false, false),
        // The entry
        account_meta(entry_address_of(entry), false, true),
        // The entry's stake account
        account_meta(entry.owned_stake_account, false, true),
        // The program authority, which is the stake authority of the stake account
        account_meta(g_program_authority_address, false, false),
        // Clock sysvar, required by the stake program
        account_meta(g_clock_sysvar_address, false, false),
        // The stake program
        account_meta(g_stake_program_address, false, false),
        // Stake history sysvar, required by the stake program
        account_meta(g_stake_history_sysvar_address, false, false),
        // The Shinobi Systems vote account
        account_meta(g_shinobi_systems_vote_address, false, false),
        // The stake config account, required by the stake program
        account_meta(g_stake_config_address, false, false)
    ];

    // The instruction data is just the instruction code
    let data = [ INSTRUCTION_DELEGATE ];

    return make_instruction(accounts, data);
}


// Returns the UpdateEpochsStaked instruction for the entry, which brings its epochs_staked up to date.  Another
// program's transaction (for example, a game's) can include it before its own instruction, after which that program
// can rely on the entry's epochs_staked being exact.  Accounts and data here must match those of
// scripts/anyone_update_epochs_staked_tx.sh
function make_update_epochs_staked_instruction(entry)
{
    let accounts = [
        // The entry
        account_meta(entry_address_of(entry), false, true),
        // The entry's stake account; for an entry that is not staked this is the system program address, which is
        // a placeholder that UpdateEpochsStaked does not use
        account_meta(entry.owned_stake_account, false, false)
    ];

    // The instruction data is just the instruction code
    let data = [ INSTRUCTION_UPDATE_EPOCHS_STAKED ];

    return make_instruction(accounts, data);
}


// Special instructions -----------------------------------------------------------------------------------------------
//
// This requires the signatures of both the admin and the owner of an entry.


// Accounts and data here must match those of scripts/special_reauthorize_tx.sh.  Moves an entry to being managed by
// another program, which requires the signatures of both the admin and the owner of the entry's token.
//
// If the entry is staked, its stake account is returned to token_owner_address (it cannot be sent anywhere else), and
// its token account is thawed, exactly as Destake would do, except that no Ki is harvested.  A Harvest instruction may
// be included before this one so that no Ki is lost.
//
// If reauthorize_metadata is true, the entry's Metaplex metadata update authority, and its mint's freeze authority, are
// passed to new_metadata_authority_address, and the entry is retired from this program.  If it is false, only the
// stake account is returned (so the entry must be staked), and new_metadata_authority_address is ignored; this exists
// so that the stake account can be recovered even if the Metaplex program were ever to fail.
//
// token_address is the token account that holds the entry's token; if it is not given, the token owner's associated
// token account for the entry's mint is used.
function make_reauthorize_instruction(entry, admin_address, token_owner_address, new_metadata_authority_address,
                                      reauthorize_metadata, token_address)
{
    if (token_address == null) {
        token_address = get_associated_token_address(token_owner_address, entry.mint_address);
    }

    // new_metadata_authority_address is ignored by the program if reauthorize_metadata is false, so allow it to be
    // omitted then
    if (new_metadata_authority_address == null) {
        if (reauthorize_metadata) {
            throw new Error("A new metadata authority is required to reauthorize metadata");
        }
        new_metadata_authority_address = g_system_program_address;
    }

    let accounts = [
        // The program state account, which identifies the admin
        account_meta(g_program_state_address, false, false),
        // The admin, who must sign
        account_meta(admin_address, true, false),
        // The entry
        account_meta(entry_address_of(entry), false, true),
        // The token owner, who must sign, and who receives the stake account if the entry is staked
        account_meta(token_owner_address, true, false),
        // The token account holding the entry's token, which is thawed if the entry is staked
        account_meta(token_address, false, true),
        // The entry's Metaplex metadata, whose update authority is changed
        account_meta(entry.metaplex_metadata_address, false, true),
        // The entry's stake account; for an entry that is not staked this is the system program address, which the
        // program ignores
        account_meta(entry.owned_stake_account, false, true),
        // The program authority
        account_meta(g_program_authority_address, false, false),
        // Clock sysvar, required by the stake program
        account_meta(g_clock_sysvar_address, false, false),
        // The Metaplex program
        account_meta(g_metaplex_program_address, false, false),
        // The stake program
        account_meta(g_stake_program_address, false, false),
        // The entry's mint, whose freeze authority is passed on with the metadata authority
        account_meta(entry.mint_address, false, true),
        // The SPL Token program
        account_meta(g_spl_token_program_address, false, false)
    ];

    // The instruction data is laid out as ReauthorizeData in program/special/special_reauthorize.c: the instruction
    // code, whether to reauthorize the metadata, and the new metadata authority
    let data = [ INSTRUCTION_REAUTHORIZE, (reauthorize_metadata ? 1 : 0),
                                    ...address_to_bytes(new_metadata_authority_address) ];

    return make_instruction(accounts, data);
}


// Byte encoding and decoding -----------------------------------------------------------------------------------------


function buffer_le_u64(buffer, offset)
{
    let ret = BigInt(0);

    for (let i = 8; i > 0; i -= 1) {
        ret = ret * 256n;
        ret += BigInt((buffer[offset + (i - 1)] & 0xFF));
    }

    return ret;
}


function buffer_le_s64(buffer, offset)
{
    return BigInt.asIntN(64, buffer_le_u64(buffer, offset));
}


// Reads a little-endian u32.  JavaScript bitwise operators work on signed 32-bit values (and take shift counts modulo
// 32), so the most significant byte is shifted by 24, and the final ">>> 0" turns the signed result into an unsigned
// one, so that values >= 2^31 are not negative.
function buffer_le_u32(buffer, offset)
{
    return ((((buffer[offset + 0] & 0xFF) <<  0) |
             ((buffer[offset + 1] & 0xFF) <<  8) |
             ((buffer[offset + 2] & 0xFF) << 16) |
             ((buffer[offset + 3] & 0xFF) << 24)) >>> 0);
}


function buffer_le_u16(buffer, offset)
{
    return (((buffer[offset + 0] & 0xFF) << 0) |
            ((buffer[offset + 1] & 0xFF) << 8));
}


function buffer_sha256(buffer, offset)
{
    return bytes_to_hex(buffer.subarray(offset, offset + 32));
}


function buffer_address(buffer, offset)
{
    return base58_encode(buffer.subarray(offset, offset + 32));
}


function buffer_string(buffer, offset, length)
{
    let result = new TextDecoder("utf-8").decode(buffer.slice(offset, offset + length));

    // To remove extra nulls, or add a null if one wasn't present
    let index = result.indexOf('\0');

    if (index >= 0) {
        return result.substring(0, index);
    }
    else {
        return result + '\0';
    }
}


// Returns the 4 little-endian bytes of a u32.  ">>>" (unsigned shift) is used so that values >= 2^31 are handled
// correctly.
function u32_to_le_bytes(u)
{
    return [ (u >>>  0) & 0xFF,
             (u >>>  8) & 0xFF,
             (u >>> 16) & 0xFF,
             (u >>> 24) & 0xFF ];
}


// Returns the 8 little-endian bytes of a u64.  u may be a BigInt, a number, or a decimal string (use a string or a
// BigInt for values that do not fit in 53 bits).  Throws if u is negative or does not fit in 64 bits.
function u64_to_le_bytes(u)
{
    let value = BigInt(u);

    if ((value < 0n) || (value > 0xFFFFFFFFFFFFFFFFn)) {
        throw new Error("Value out of u64 range: " + u);
    }

    let ret = [ ];

    for (let i = 0; i < 8; i += 1) {
        ret.push(Number(value & 0xFFn));
        value >>= 8n;
    }

    return ret;
}


function u16_to_le_bytes(u)
{
    return [ (u >>  0) & 0xFF,
             (u >>  8) & 0xFF ];
}


// Returns the 32 bytes of a SHA-256, given as a 64 character hex string or as 32 bytes (an array, Uint8Array, or
// Buffer).  Throws if it is neither.
function sha256_to_bytes(sha256)
{
    if (typeof(sha256) == "string") {
        if (!/^[0-9a-fA-F]{64}$/.test(sha256)) {
            throw new Error("Invalid SHA-256 hex string: " + sha256);
        }
        return [ ...hex_to_bytes(sha256) ];
    }

    if ((sha256 == null) || (sha256.length != 32)) {
        throw new Error("A SHA-256 must be 32 bytes");
    }

    return [ ...sha256 ];
}


// Returns the bytes of a fixed size C string field of length bytes: the UTF-8 encoding of str, padded with zero bytes
// to length.  A string that fills the field has no terminating zero, as in the program.  Throws if the UTF-8 encoding
// of str is longer than length.
function c_string_to_bytes(str, length)
{
    let bytes = [ ...utf8_to_bytes(str) ];

    if (bytes.length > length) {
        throw new Error("String is longer than " + length + " bytes: " + str);
    }

    while (bytes.length < length) {
        bytes.push(0);
    }

    return bytes;
}


// Exports ------------------------------------------------------------------------------------------------------------

exports.LAMPORTS_PER_SOL = LAMPORTS_PER_SOL;
exports.base58_encode = base58_encode;
exports.base58_decode = base58_decode;
exports.to_address = to_address;
exports.address_to_bytes = address_to_bytes;
exports.utf8_to_bytes = utf8_to_bytes;
exports.bytes_to_hex = bytes_to_hex;
exports.hex_to_bytes = hex_to_bytes;
exports.sha256 = sha256;
exports.is_on_curve = is_on_curve;
exports.sha512 = sha512;
exports.ed25519_verify = ed25519_verify;
exports.create_program_address = create_program_address;
exports.find_pda = find_pda;
exports.AccountRole = AccountRole;
exports.account_meta = account_meta;
exports.make_instruction = make_instruction;
exports.g_system_program_address = g_system_program_address;
exports.g_self_program_address = g_self_program_address;
exports.g_metaplex_program_address = g_metaplex_program_address;
exports.g_spl_token_program_address = g_spl_token_program_address;
exports.g_spl_associated_token_program_address = g_spl_associated_token_program_address;
exports.g_stake_program_address = g_stake_program_address;
exports.g_config_program_address = g_config_program_address;
exports.g_validator_info_address = g_validator_info_address;
exports.g_program_authority_address = g_program_authority_address;
exports.g_program_state_address = g_program_state_address;
exports.g_shinobi_systems_vote_address = g_shinobi_systems_vote_address;
exports.g_ki_mint_address = g_ki_mint_address;
exports.g_ki_metadata_address = g_ki_metadata_address;
exports.g_bid_marker_mint_address = g_bid_marker_mint_address;
exports.g_bid_marker_metadata_address = g_bid_marker_metadata_address;
exports.g_rent_sysvar_address = g_rent_sysvar_address;
exports.g_clock_sysvar_address = g_clock_sysvar_address;
exports.g_stake_history_sysvar_address = g_stake_history_sysvar_address;
exports.g_stake_config_address = g_stake_config_address;
exports.INSTRUCTION_INITIALIZE = INSTRUCTION_INITIALIZE;
exports.INSTRUCTION_SET_ADMIN = INSTRUCTION_SET_ADMIN;
exports.INSTRUCTION_CREATE_BLOCK = INSTRUCTION_CREATE_BLOCK;
exports.INSTRUCTION_ADD_ENTRIES_TO_BLOCK = INSTRUCTION_ADD_ENTRIES_TO_BLOCK;
exports.INSTRUCTION_SET_METADATA_BYTES = INSTRUCTION_SET_METADATA_BYTES;
exports.INSTRUCTION_REVEAL_ENTRIES = INSTRUCTION_REVEAL_ENTRIES;
exports.INSTRUCTION_ADD_WHITELIST_ENTRIES = INSTRUCTION_ADD_WHITELIST_ENTRIES;
exports.INSTRUCTION_DELETE_WHITELIST = INSTRUCTION_DELETE_WHITELIST;
exports.INSTRUCTION_WITHDRAW_EXCESS = INSTRUCTION_WITHDRAW_EXCESS;
exports.INSTRUCTION_BUY = INSTRUCTION_BUY;
exports.INSTRUCTION_REFUND = INSTRUCTION_REFUND;
exports.INSTRUCTION_BID = INSTRUCTION_BID;
exports.INSTRUCTION_CLAIM_LOSING = INSTRUCTION_CLAIM_LOSING;
exports.INSTRUCTION_STAKE = INSTRUCTION_STAKE;
exports.INSTRUCTION_DESTAKE = INSTRUCTION_DESTAKE;
exports.INSTRUCTION_THAW = INSTRUCTION_THAW;
exports.INSTRUCTION_HARVEST = INSTRUCTION_HARVEST;
exports.INSTRUCTION_LEVEL_UP = INSTRUCTION_LEVEL_UP;
exports.INSTRUCTION_SETTLE_WINNING_BID = INSTRUCTION_SETTLE_WINNING_BID;
exports.INSTRUCTION_DELEGATE = INSTRUCTION_DELEGATE;
exports.INSTRUCTION_UPDATE_EPOCHS_STAKED = INSTRUCTION_UPDATE_EPOCHS_STAKED;
exports.INSTRUCTION_REAUTHORIZE = INSTRUCTION_REAUTHORIZE;
exports.DATA_TYPE_AUTHORITY = DATA_TYPE_AUTHORITY;
exports.WithdrawExcessAccountType = WithdrawExcessAccountType;
exports.get_metaplex_metadata_address = get_metaplex_metadata_address;
exports.get_block_address = get_block_address;
exports.get_whitelist_address = get_whitelist_address;
exports.get_entry_mint_address = get_entry_mint_address;
exports.get_entry_address = get_entry_address;
exports.get_entry_token_address = get_entry_token_address;
exports.get_associated_token_address = get_associated_token_address;
exports.get_bid_marker_token_address = get_bid_marker_token_address;
exports.get_bid_address = get_bid_address;
exports.entry_address_of = entry_address_of;
exports.block_address_of = block_address_of;
exports.decode_program_state = decode_program_state;
exports.decode_authority = decode_authority;
exports.decode_block = decode_block;
exports.decode_entry = decode_entry;
exports.KI_HARVEST_RETENTION_ONE = KI_HARVEST_RETENTION_ONE;
exports.compute_ki_harvest = compute_ki_harvest;
exports.compute_retained_ki_harvest = compute_retained_ki_harvest;
exports.compute_ki_harvest_retention = compute_ki_harvest_retention;
exports.compute_effective_stake_earnings = compute_effective_stake_earnings;
exports.compute_price = compute_price;
exports.compute_minimum_bid = compute_minimum_bid;
exports.excess_of = excess_of;
exports.make_initialize_instruction = make_initialize_instruction;
exports.make_set_admin_instruction = make_set_admin_instruction;
exports.make_create_block_instruction = make_create_block_instruction;
exports.make_add_entries_to_block_instruction = make_add_entries_to_block_instruction;
exports.make_set_metadata_bytes_instruction = make_set_metadata_bytes_instruction;
exports.make_reveal_entries_instruction = make_reveal_entries_instruction;
exports.MAX_WHITELIST_ADD_ENTRIES = MAX_WHITELIST_ADD_ENTRIES;
exports.make_add_whitelist_entries_instruction = make_add_whitelist_entries_instruction;
exports.make_delete_whitelist_instruction = make_delete_whitelist_instruction;
exports.make_withdraw_excess_instruction = make_withdraw_excess_instruction;
exports.make_buy_instruction = make_buy_instruction;
exports.make_refund_instruction = make_refund_instruction;
exports.make_bid_instruction = make_bid_instruction;
exports.make_claim_losing_instruction = make_claim_losing_instruction;
exports.make_stake_instruction = make_stake_instruction;
exports.make_destake_instruction = make_destake_instruction;
exports.make_thaw_instruction = make_thaw_instruction;
exports.make_harvest_instruction = make_harvest_instruction;
exports.make_level_up_instruction = make_level_up_instruction;
exports.make_settle_winning_bid_instruction = make_settle_winning_bid_instruction;
exports.make_delegate_instruction = make_delegate_instruction;
exports.make_update_epochs_staked_instruction = make_update_epochs_staked_instruction;
exports.make_reauthorize_instruction = make_reauthorize_instruction;
exports.buffer_le_u64 = buffer_le_u64;
exports.buffer_le_s64 = buffer_le_s64;
exports.buffer_le_u32 = buffer_le_u32;
exports.buffer_le_u16 = buffer_le_u16;
exports.buffer_sha256 = buffer_sha256;
exports.buffer_address = buffer_address;
exports.buffer_string = buffer_string;
exports.u32_to_le_bytes = u32_to_le_bytes;
exports.u64_to_le_bytes = u64_to_le_bytes;
exports.u16_to_le_bytes = u16_to_le_bytes;
exports.sha256_to_bytes = sha256_to_bytes;
exports.c_string_to_bytes = c_string_to_bytes;
