#!/bin/bash
# Require the selected runtime rather than silently testing another release.
set -euo pipefail
# Read the release name and executable hashes from the same committed manifest.
SDK_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
LOCK=$SDK_DIR/toolchain.lock.json
VERSION=$(jq -er .agave_version "$LOCK")
# Reject an empty/malformed manifest rather than silently checking no executables.
jq -e '.linux_x86_64_agave_binaries | type == "object" and length > 0' "$LOCK" >/dev/null
# jq emits tab-separated filename/hash pairs; process substitution keeps the loop
# in this shell, so a mismatch terminates this verifier immediately.
while IFS=$'\t' read -r name expected; do
    # Check the executable actually selected by PATH, not an unused cached release.
    path=$(command -v "$name")
    actual=$(sha256sum "$path")
    if [ "${actual%% *}" != "$expected" ]; then
        echo "$name is not the pinned Agave $VERSION binary; check PATH" >&2
        exit 1
    fi
    # Print the verified version so a test log identifies the runtime it exercised.
    "$path" --version
done < <(jq -r '.linux_x86_64_agave_binaries | to_entries[] | [.key,.value] | @tsv' "$LOCK")
