#!/bin/bash
# Build a C SBPFv3 program: build.sh SOURCE.c OUTPUT.so [CLANG_ARGUMENT ...]
set -euo pipefail
# Resolve our include files and verifier scripts independently of the caller's directory.
SDK_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# Require both paths before creating any temporary build artifacts.
SOURCE_FILE=${1:?Usage: sdk/build.sh SOURCE.c OUTPUT.so [CLANG_ARGUMENT ...]}
OUTPUT_FILE=${2:?Usage: sdk/build.sh SOURCE.c OUTPUT.so [CLANG_ARGUMENT ...]}
# The remaining positional arguments go directly to Clang. Quoted "$@" keeps
# each definition intact, including strings with spaces and public-key arrays.
# Do not combine these arguments into a string or re-interpret them with eval.
shift 2
# Allow a relocated installation, but require exactly the locked compiler bytes.
PLATFORM_TOOLS=${PLATFORM_TOOLS:-${XDG_CACHE_HOME:-$HOME/.cache}/solana/v1.57/platform-tools}
bash "$SDK_DIR/verify_toolchain.sh" "$PLATFORM_TOOLS"
# Keep intermediate objects private to this invocation and remove them even on failure.
mkdir -p "$(dirname "$OUTPUT_FILE")"
BUILD_TMP=$(mktemp -d)
trap 'rm -rf "$BUILD_TMP"' EXIT
# Select the Solana LLVM target and v3 instructions; SOL_SBPFV3 selects static
# syscall wrappers in the C headers. Compile without a host C runtime, implicit
# library calls, or a stack-protector runtime. Retain the existing initialized
# data policy and hide ordinary symbols; the ELF entrypoint is selected below.
# Put our adapter include directory first, then the unmodified upstream headers.
"$PLATFORM_TOOLS/llvm/bin/clang" -target sbf -mcpu=v3 -DSOL_SBPFV3 \
    -std=c2x -O3 -ffreestanding -fno-builtin -fno-stack-protector \
    -fno-zero-initialized-in-bss -fno-data-sections -fvisibility=hidden \
    -I"$SDK_DIR/include" -I"$SDK_DIR/upstream" \
    "$@" \
    -c "$SOURCE_FILE" -o "$BUILD_TMP/program.o"
# Use the upstream-derived v3 segment layout and symbolic binding. The linker
# must reject unresolved symbols here instead of deferring discovery to runtime.
# The single linker thread also removes a source of build-process variability.
"$PLATFORM_TOOLS/llvm/bin/ld.lld" --threads=1 -z notext --Bdynamic -Bsymbolic \
    --no-undefined --entry entrypoint -T "$SDK_DIR/sbpfv3.ld" \
    -o "$OUTPUT_FILE" "$BUILD_TMP/program.o"
# Inspect the emitted file, not just the compiler options, before reporting success.
bash "$SDK_DIR/verify_elf.sh" "$OUTPUT_FILE"
echo "Built SBPFv3: $OUTPUT_FILE"
