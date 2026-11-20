// What the reference programs share: a seeded random generator, and hex output.

// A random generator (xorshift64*), seeded from a string, so that a program's vectors are the same for the same seed.
// Its quality is plenty for test inputs.
pub struct Rng(u64);

impl Rng {
    // Returns a generator seeded from seed (the test run's seed), by FNV-1a hashing it, and a name, so that each
    // program gets its own values from the same seed
    pub fn new(seed: &str, name: &str) -> Rng {
        let mut hash: u64 = 0xcbf29ce484222325;
        for byte in seed.bytes().chain(":".bytes()).chain(name.bytes()) {
            hash ^= byte as u64;
            hash = hash.wrapping_mul(0x100000001b3);
        }
        // xorshift needs a nonzero state
        Rng(if hash == 0 { 1 } else { hash })
    }

    pub fn next(&mut self) -> u64 {
        self.0 ^= self.0 >> 12;
        self.0 ^= self.0 << 25;
        self.0 ^= self.0 >> 27;
        self.0.wrapping_mul(0x2545f4914f6cdd1d)
    }

    pub fn below(&mut self, n: usize) -> usize {
        (self.next() % (n as u64)) as usize
    }

    pub fn bytes(&mut self, n: usize) -> Vec<u8> {
        (0..n).map(|_| (self.next() >> 32) as u8).collect()
    }

    pub fn bytes32(&mut self) -> [u8; 32] {
        let mut b = [0u8; 32];
        b.copy_from_slice(&self.bytes(32));
        b
    }
}

// Returns bytes in hex, or "-" for no bytes (so that every field of a vector line is nonempty)
pub fn hex(bytes: &[u8]) -> String {
    if bytes.is_empty() {
        return "-".to_string();
    }
    bytes.iter().map(|b| format!("{:02x}", b)).collect()
}

// Returns the seed given as the program's only argument
pub fn seed_argument() -> String {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 2 {
        eprintln!("Usage: {} <seed>", args[0]);
        std::process::exit(1);
    }
    args[1].clone()
}
