#!/bin/bash
# Fail closed on changed upstream headers or unpinned compiler/linker binaries.
set -euo pipefail
# Resolve the manifest relative to this script and require a compiler installation path.
SDK_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
TOOLS=${1:?Usage: verify_toolchain.sh PLATFORM_TOOLS}
LOCK=$SDK_DIR/toolchain.lock.json
# Executable digests are specific to this supported host platform.
if [ "$(uname -s)/$(uname -m)" != Linux/x86_64 ]; then
    echo 'The verified compiler pin currently supports Linux x86_64 only' >&2
    exit 1
fi
# An empty manifest must not turn verification into a successful zero-iteration loop.
jq -e '.linux_x86_64_binaries | type == "object" and length > 0' "$LOCK" >/dev/null
jq -e '.upstream_header_sha256 | type == "object" and length > 0' "$LOCK" >/dev/null
# Hash the actual compiler and linker, extracting sha256sum's digest before its filename.
while IFS=$'\t' read -r name expected; do
    actual=$(sha256sum "$TOOLS/llvm/bin/$name")
    if [ "${actual%% *}" != "$expected" ]; then
        echo "Compiler checksum mismatch: $TOOLS/llvm/bin/$name" >&2
        exit 1
    fi
done < <(jq -r '.linux_x86_64_binaries | to_entries[] | [.key,.value] | @tsv' "$LOCK")
# Separately verify every archived header/source file; local adaptations belong
# in sdk/include, so upstream edits must trigger a provenance/lock review.
while IFS=$'\t' read -r name expected; do
    actual=$(sha256sum "$SDK_DIR/upstream/$name")
    if [ "${actual%% *}" != "$expected" ]; then
        echo "Upstream header changed: $name; review provenance and lock" >&2
        exit 1
    fi
done < <(jq -r '.upstream_header_sha256 | to_entries[] | [.key,.value] | @tsv' "$LOCK")
