# SBPFv3 C validation — 2026-11-12

Completed migration steps 1–4 on Linux x86_64:

- Active CLI, keygen, and test validator: **Agave 4.4.0**, `9ef63128`.
- Compiler/linker: pinned **platform-tools v1.57**; executable hashes and official
  release archive digests recorded in `toolchain.lock.json`.
- All **43** vendored upstream header/source files match their locked hashes.
- Both C test programs link without undefined symbols and pass the SBPFv3 ELF
  checks (flags `0x3`, read-only data at 0, code at `0x100000000`).
- Smoke transaction passed on the validator: legacy account/input deserialization,
  static log syscall, clock/rent sysvar calls, SHA-256, memory comparison,
  return-data set/get and returned program identity. The driver also verified
  the persisted transaction log and returned byte.
- **2,605 calculation cases** executed in **163 signed transactions**, plus the
  smoke transaction. Reference values were generated with Bash and `dc` integers;
  production calculations and result comparisons executed in C under SBPFv3.

Command:

```sh
bash test/test_calculations.sh
# Equivalent: make test-calculations
```

Result:

```text
2601 PASS, 4 XFAIL, 0 FAIL, 0 XPASS; 0 transaction/runtime failures
```

Expected failures, each printed explicitly while subsequent cases continued:

| Case | Actual | Expected |
| --- | ---: | ---: |
| Price minimum rounding | 1,001,000 | 1,001,999 |
| Ki negative reduction | 172,938,332,659,110 | 0 |
| Commission multiplication overflow | 18,520,728,256,510 | 300,000,000,000,000 |
| Commission on decreased balance | 281,479,261,744,251 | 0 |

All four returned overflow=false; their annotations identify the existing
arithmetic defects. A future fix yields XPASS and fails the run until its
expected-failure annotation is reviewed.

Bash syntax checks and `git diff --check` passed. The test runner cleaned
up its disposable validator and ledger. No host-side C unit executable was used.

Revalidated after removing Python from the entire calculation test path:
vector generation, transaction submission/confirmation, toolchain checks,
ELF checks, and validator startup now use shell tools. The complete run exited
successfully with the totals above. Random inputs now use a fixed-seed 64-bit
LCG in `dc`; edge cases, case counts, and expected defects are retained.
Repeated vector generation is byte-identical. The rebuilt calculation program
is byte-identical to the previous build. ELF verifier checks also correctly
rejected a wrong SBPF version, truncated header, and invalid segment layout.

This establishes the standalone C build and calculation tests, **not** full
production compatibility. The original program build, production CPI and account
mutation behavior, complete instruction suite, and reproducible VM build remain
for migration steps 5 and 6. No security defects were fixed as part of this work.
