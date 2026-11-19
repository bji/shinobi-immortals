
Tests are executed against a local test validator.

Calculation integration tests
-----------------------------
Run: make test-calculations
Or:  bash test/test_calculations.sh --verbose

test/unit_tests.c is a separate C BPF program, not a host executable. It includes
the production calculations, receives vectors in instruction data, checks results
inside BPF, and returns per-case results. Bash and dc generate independent
reference values using arbitrary-precision integers; they never execute
production code.

The runner uses the pinned C/SBPFv3 build in sdk/build.sh (see sdk/README.md),
starts a disposable Agave 4.4.0 validator, deploys both the smoke and calculation
programs to finalized commitment, verifies a smoke transaction, then sends
real signed transactions using solxact, and checks confirmed execution metadata
and return data. No mainnet accounts or programs are cloned. Cases are batched
16 per transaction. Unexpected arithmetic failures, unexpected passes, submission
errors, missing confirmations, and runtime errors fail the suite. Failed batches
do not prevent later batches from running.

Known defects print XFAIL with "currently expected to fail" and a reason. They
do not fail the suite. XPASS fails the suite so fixed defects must have their
expected-failure annotations reviewed. There are currently no expected defects;
price floor rounding was fixed.

Ki calculations scale earnings by effective total stake, using a 50 SOL
asymptote K: effective(x) = K*x*(K+x)/(K*K+K*x+x*x).
The curve input x and K are measured in units of 100 lamports, made ten times
larger for as long as x exceeds 2,000,000,000 units, which keeps all
calculations within uint64_t. K is exact in every unit, and x is rounded down
by less than 0.0000001 SOL up to 200 SOL and by less than one part in 200
million above that.
The ratio is applied to earned lamports first. Effective earnings are then
converted to whole Ki with ki_factor, preserving fractional SOL until after
multiplication. Only the final mint uses ten token base units per whole Ki.
Operation 4 takes earned lamports in a and current stake lamports in b.
Operation 16 converts effective lamports (a) with a uint32_t factor (b).
Operation 17 checks the complete pipeline: earned lamports (a), current stake
lamports (b), and ki_factor (c). Vectors include 500 epochs of earnings,
50 effective SOL with UINT32_MAX factor, and full-range stake balances.

Requirements: pinned Agave 4.4.0 and platform-tools v1.57 on Linux x86_64,
solxact, Bash, dc, jq, curl, and standard Linux coreutils.
Setup: bash sdk/install_toolchain.sh.
Headers and the SBPFv3 linker script are maintained in sdk/. SDK_ROOT and
BPF_TOOLS are no longer used by the standalone calculation tests. PLATFORM_TOOLS
can select a different directory containing the identical pinned compiler files.

Each run selects a free private port block and verifies the RPC server's identity
against its own ledger. CALCULATION_RPC_PORT selects the block's base explicitly:
RPC base, PubSub base+1, faucet base+2, gossip base+3, dynamic base+10..base+60.
Occupied blocks are rejected. The runner stops only its own validator PID.
Set KEEP_CALCULATION_LEDGER=1 to retain artifacts, logs, and ledger for diagnosis.
--limit N runs only the first N cases; --verbose prints every individual result.

To compile only: mkdir -p /tmp/codex; TMPDIR=/tmp/codex bash test/build_calculation_tests.sh /tmp/codex/calculation-build
To inspect the vectors without a validator: bash test/calculation_vectors.sh

Vectors are pipe-delimited rows: name, expected-failure reason (possibly empty),
operation, initial-overflow, expected-failure flag, expected-overflow, four
arguments, expected value. A fixed-seed 64-bit LCG supplies repeatable random
inputs; dc performs all wide arithmetic, including overflow reference results.
Operations: 0 add, 1 multiply, 2 price, 3 minimum bid, 4 Ki, (5 unused),
6 window level credits, 7 cumulative level requirement, 8 multiply fraction.
The driver keeps binary transactions in files and decodes returned unsigned
64-bit values with od, without lossy shell/JSON numeric conversions.
