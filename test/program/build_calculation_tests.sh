#!/bin/bash
# Build the calculation test program using the pinned repository-owned C SDK.
set -euo pipefail
# Locate the repository when invoked from a temporary validator ledger directory.
SOURCE_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
# Keep the artifact name stable for the deployment runner, using the shared build flags.
OUTPUT_DIR=${1:?Usage: build_calculation_tests.sh OUTPUT_DIR}
bash "$SOURCE_ROOT/build_tools/sdk/build.sh" "$SOURCE_ROOT/test/program/unit_tests.c" "$OUTPUT_DIR/calculation_tests.so"
