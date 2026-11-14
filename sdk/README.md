# Pinned C support for SBPFv3

This directory supports the standalone C smoke and calculation transaction tests.
It does **not** switch the production program build to SBPFv3. Migrating the full
program and establishing VM-based reproducible builds are separate work.

## Toolchain

- Agave CLI, keygen and test validator: **4.4.0**, release commit `9ef63128`.
- Anza platform-tools: **v1.57**; Clang **22.1.2-rust-dev**, LLVM commit
  `a4135d044fb7a19f2b7e7d04d0373cd2ba190e6e`.
- C target: `sbf`, CPU `v3`, static syscalls (`SOL_SBPFV3`).
- Account/entrypoint ABI: existing ABI v1, not experimental ABI v2.
- Verified installation/build platform: Linux x86_64.

`toolchain.lock.json` records official release asset SHA-256 digests, installed
compiler/linker and Agave executable hashes, and every upstream header hash.
Builds reject a different compiler/linker; the transaction runner rejects a
different validator/CLI. No Rust compiler or Rust program code is used by the
C build. `cargo-build-sbf --install-only --tools-version v1.57` can optionally
install the same LLVM distribution, but cargo-build-sbf itself is not used to
compile or link these programs.

Setup on a new Linux x86_64 machine:

```sh
agave-install init 4.4.0 --no-modify-path
# Ensure the installed active_release/bin is first in PATH.
bash sdk/install_toolchain.sh
bash sdk/verify_agave.sh
```

The installer verifies the platform-tools download against its locked release
digest before extraction. The default compiler location is
`~/.cache/solana/v1.57/platform-tools` (respects `XDG_CACHE_HOME`). Set
`PLATFORM_TOOLS` to a different location containing the identical pinned files.

## Header provenance and adapter

`upstream/` is an **unmodified** copy of
`platform-tools-sdk/sbf/c/inc` from Anza Agave **v3.0.14**. These headers already
contain static-syscall definitions guarded by `SOL_SBPFV3`. Their generated
binding addresses are retained; they are not dynamically resolved at runtime.
The archived header package is 180 KiB, has no external on-chain dependencies,
and is checked into this repository so installed CLI packages need not provide it.

Source archive:
<https://github.com/anza-xyz/agave/archive/refs/tags/v3.0.14.tar.gz>

Its SHA-256 and individual header hashes are in the lock file. `LICENSE` is the
Apache-2.0 license copied from that release. Upstream URLs and licenses are
retained rather than attributing these headers to this project.

`include/solana_sdk.h` is the repository-owned adapter. It requires SBPFv3,
includes the pinned umbrella header, and supplies the missing clock and rent
sysvar wrappers using static Murmur3-32 syscall addresses. Other types and
helpers, including account deserialization and CPI construction, remain the
upstream definitions. Their full runtime compatibility still needs the production
instruction integration tests in the later migration step.

`sbpfv3.ld` derives from the `V3_LINKER_SCRIPT` in Anza's Rust target definition:
<https://github.com/anza-xyz/rust/blob/3175de2636b6e1145b7655fd4131095e23452137/compiler/rustc_target/src/spec/base/sbf_base.rs#L31-L63>

That source is dual MIT/Apache-2.0; this copy is used under Apache-2.0.
The script places read-only data at 0 and code at `0x100000000`, discarding
legacy dynamic linking sections. Linking rejects undefined symbols.
`verify_elf.sh` checks SBPFv3 flags (`0x3`) and the segment layout after every build.

## Execution

```sh
bash sdk/build.sh test/sbpfv3_smoke.c /tmp/smoke.so -Iprogram
bash test/build_calculation_tests.sh /tmp/calculation-build
make test-calculations
```

Arguments after the source and output paths are passed directly to Clang. Use
the same `-D` definitions as in `build_program.sh`, quoting each entire argument
when its value contains spaces or shell-special characters:

```sh
bash sdk/build.sh source.c /tmp/program.so \
    -DSTATE_BUMP_SEED=253 \
    '-DSUPERUSER_PUBKEY_ARRAY={149, 75, 37}' \
    '-DMESSAGE="hello world"'
```

The shortened array above illustrates quoting; supply all 32 bytes for an actual
public key. Existing shell variables can be used directly, for example
`-DSUPERUSER_PUBKEY_ARRAY="$SUPERUSER_PUBKEY_C_ARRAY"`. Arguments are forwarded
individually without `eval`; these compiler arguments are not passed to the linker.

`make test-calculations` starts an isolated Agave 4.4.0 validator, deploys both C programs,
executes a smoke transaction and the calculation vectors, and validates their
confirmed execution metadata and return data. Bash and `dc` supply reference
inputs and arbitrary-precision expected results; production calculations execute
under SBPFv3. JSON is handled by `jq`; no Python interpreter is required by the
build, installer, startup checks, or transaction driver. The scripts also use
standard Linux tools including `curl`, `sha256sum`, `od`, `wc`, and `base64`.
All 64-bit values are kept as decimal strings until serialized: neither
Bash's signed arithmetic nor jq's floating-point numbers compute test results.
Known failures are XFAIL; unexpected failures, unexpected passes, transaction
failures and missing results fail the run. See `test/README.txt` for controls.
