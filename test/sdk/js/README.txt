Tests of the Shinobi Immortals Javascript SDK (sdk/js)
-------------------------------------------------------

Run them all with:

    test/sdk/js/run_tests.sh

or with "make test-sdk-js".  They take about 15 seconds, most of it generating the calculation vectors.

They need node, bash, dc, a C compiler (cc), and solxact (https://github.com/bji/solxact), all of which the program
tests (test/program) need too.  test_primitives.js also needs Rust's cargo, to build and run the reference programs
of reference/ (see below); the first run builds them, which downloads the crates that reference/Cargo.lock pins if
cargo does not already have them.  test_kit.js, test_rpc.js, and test_wallet.js also need @solana/kit from npm, pinned
in package.json and package-lock.json; install it with "make sdk-js-packages" (in the top directory), which runs
"npm ci --ignore-scripts" here.  Until it is installed, those three tests are skipped (and say so).  Install it only
that way, not with a bare "npm ci" or "npm install": --ignore-scripts keeps npm from running any package's install
scripts, which are a common way for a compromised package to run code.  "make clean-test-sdk-js" removes the
reference programs' build output (reference/target), and "make clean-sdk-js-packages" removes the npm packages.
sdk/js/shinobi_immortals_base.js itself has no dependencies; sdk/js/shinobi_immortals.js uses @solana/kit.

Each test checks the SDK against something independent of it, mostly the program's own sources and the scripts that
the program tests use:

  test_primitives.js           SHA-256 and SHA-512 against Node's crypto module; base58 against Bitcoin's published
                               test vectors; the ed25519 curve check, and ed25519 signature verification, against
                               Solana's own (see reference/), and signature verification also against Node's
                               ed25519; and program derived addresses, including every address derivation function
                               of the SDK, against "solxact pda".

  test_program_consistency.js  The instruction codes, data types, WithdrawExcess account types, and whitelist limit
                               against the program's C source, and that every instruction has a builder; the fixed
                               addresses and bump seeds against those compiled into the program (build_program.sh);
                               and the account data decoders against the program's structure layouts, by decoding
                               structures made by layouts.c, which is compiled with the program's own headers.

  test_calculations.js         The price, bid, effective stake, and Ki harvest calculations against the reference
                               vectors that test/program/calculation_vectors.sh generates for the program's own
                               calculation tests.

  test_instructions.js         Every instruction builder, with its options, against the transaction that the
                               corresponding script in scripts/ makes, decoded by "solxact decode": the program, every
                               account and its permissions, and the data.

  test_kit.js (npm)            That the SDK's instructions work directly with @solana/kit: each kind is put into
                               legacy and version 0 transactions by @solana/kit, and decoded back unchanged.

  test_rpc.js (npm)            How sdk/js/shinobi_immortals.js reads the cluster, against a mock RPC server (through
                               @solana/kit's RPC client) holding a small cluster state: that each RpcConnection
                               method makes the right request and returns its documented form, and that Cluster,
                               Entry, and Wallet find and decode blocks, entries, balances, Ki, bids, and stake
                               accounts.

  test_wallet.js (npm)         The transactions of sdk/js/shinobi_immortals.js, against a mock RPC server: that each
                               transaction method of Wallet sends exactly the instruction of the corresponding
                               builder, validly signed; that create_callback_signer works with a signature made by
                               solxact from the transaction it is given; that invalid signatures, changed
                               transactions, and the wrong signer are refused; that a blockhash that expires before
                               signing aborts the signing and starts again with a new blockhash; and that with
                               WebCrypto disabled, a transaction signed through a callback signer is still checked
                               and sent.

Each test prints "PASS: <test>: <n> checks", or "FAIL: <test>: <check>: <detail>" for each check that fails.  Set
VERBOSE=1 to see every check.  The tests use random inputs from a seeded generator; a failing run prints its seed,
and setting TEST_SEED to it repeats the run exactly.  A single test can be run directly, e.g.
"node test/sdk/js/test_instructions.js".

common.js holds what the tests share.

reference/ is a Rust project (built with cargo) of two programs, which print test vectors whose expected results are
computed by the Solana crates that define them, so that the SDK's own implementations are checked against exactly
what Solana does:

  curve_points                 Whether 32 bytes are a point on the ed25519 curve, by solana-address's
                               bytes_are_curve_point, which decides which addresses can be program derived
                               addresses.

  ed25519_signatures           Whether signatures are valid, by solana-signature's Signature::verify, which the Solana
                               runtime uses to verify transaction signatures; the vectors include every kind of
                               signature that it rejects.

Each takes the run's seed, and prints the same edge cases every time plus new random values made from the seed.  The
comments at the top of each program say exactly what it prints.  The crates' versions are pinned in
reference/Cargo.toml and reference/Cargo.lock; to check against a newer Solana (for example after a change to its
signature verification rules), update those and run the tests.
