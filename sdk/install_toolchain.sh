#!/bin/bash
# Download the exact Linux x86_64 platform-tools artifact, verifying its release
# SHA-256 before extraction. Existing pinned installations are left alone.
set -euo pipefail
# Find the committed lock file beside this script, regardless of the working directory.
SDK_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# Match the platform-tools cache convention while allowing an explicitly relocated copy.
PLATFORM_TOOLS=${PLATFORM_TOOLS:-${XDG_CACHE_HOME:-$HOME/.cache}/solana/v1.57/platform-tools}
# Reuse an existing installation only if its binaries and our headers still match the lock.
if [ -x "$PLATFORM_TOOLS/llvm/bin/clang" ]; then
    bash "$SDK_DIR/verify_toolchain.sh" "$PLATFORM_TOOLS"
    exit
fi
# Only the Linux x86_64 artifact has executable-level pins in this project.
if [ "$(uname -s)/$(uname -m)" != Linux/x86_64 ]; then
    echo 'The pinned and tested toolchain currently requires Linux x86_64' >&2
    exit 1
fi
# Download into a disposable directory so unverified archive contents cannot be used.
DOWNLOAD_TMP=$(mktemp -d)
trap 'rm -rf "$DOWNLOAD_TMP"' EXIT
# Fetch a fixed release, following GitHub's asset redirect and retrying transport failures.
curl -fL --retry 2 https://github.com/anza-xyz/platform-tools/releases/download/v1.57/platform-tools-linux-x86_64.tar.bz2 \
    -o "$DOWNLOAD_TMP/platform-tools.tar.bz2"
# Release digests use a "sha256:" prefix; sha256sum also prints the filename,
# so compare only its first output field after adding that prefix.
expected=$(jq -er '.platform_tools_assets["platform-tools-linux-x86_64.tar.bz2"]' "$SDK_DIR/toolchain.lock.json")
actual=$(sha256sum "$DOWNLOAD_TMP/platform-tools.tar.bz2")
if [ "sha256:${actual%% *}" != "$expected" ]; then
    echo 'Platform-tools download checksum mismatch' >&2
    exit 1
fi
# Extract only after validating the whole archive, then check the installed executables too.
mkdir -p "$PLATFORM_TOOLS"
tar -xjf "$DOWNLOAD_TMP/platform-tools.tar.bz2" -C "$PLATFORM_TOOLS"
bash "$SDK_DIR/verify_toolchain.sh" "$PLATFORM_TOOLS"
