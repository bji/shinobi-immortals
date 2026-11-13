#!/bin/bash
# Standalone transaction-based calculation suite. Starts its own validator;
# no mainnet clones, Shinobi program deployment, or production wallets needed.
set -euo pipefail
# Resolve source paths before changing anything, and isolate keys, logs and ledger.
SOURCE_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
CALCULATION_TMP=$(mktemp -d)
VALIDATOR_PID=
function cleanup ()
{
    # Stop only the child started by this invocation; never kill validators by name.
    if [ -n "$VALIDATOR_PID" ]; then
        kill "$VALIDATOR_PID" 2>/dev/null || true
        # Reap the child before deleting files it may still have open.
        wait "$VALIDATOR_PID" 2>/dev/null || true
    fi
    # Allow failed runs to retain their evidence when explicitly requested.
    if [ "${KEEP_CALCULATION_LEDGER:-0}" = 1 ]; then
        echo "Retained calculation artifacts: $CALCULATION_TMP"
    else
        rm -rf "$CALCULATION_TMP"
    fi
}
# Normal exits and signal-triggered exits all pass through the same cleanup path.
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# Verify the selected runtime first, then compile the smoke and calculation programs.
bash "$SOURCE_ROOT/sdk/verify_agave.sh"
# The smoke source uses the project's Clock type; supply that include path here
# so the shared SDK builder does not depend on this repository's program layout.
bash "$SOURCE_ROOT/sdk/build.sh" "$SOURCE_ROOT/test/sbpfv3_smoke.c" "$CALCULATION_TMP/smoke.so"                      \
    -I"$SOURCE_ROOT/program"
bash "$SOURCE_ROOT/test/build_calculation_tests.sh" "$CALCULATION_TMP"
# All accounts are disposable. Separate deployment buffers avoid CLI-generated
# recovery phrases and ensure any deployment state stays in this temporary directory.
solana-keygen new --no-bip39-passphrase --silent -o "$CALCULATION_TMP/program.json" >/dev/null
solana-keygen new --no-bip39-passphrase --silent -o "$CALCULATION_TMP/payer.json" >/dev/null
solana-keygen new --no-bip39-passphrase --silent -o "$CALCULATION_TMP/smoke.json" >/dev/null
solana-keygen new --no-bip39-passphrase --silent -o "$CALCULATION_TMP/smoke-buffer.json" >/dev/null
solana-keygen new --no-bip39-passphrase --silent -o "$CALCULATION_TMP/calculation-buffer.json" >/dev/null
# Public addresses identify the deployed programs and the genesis-funded fee payer.
SMOKE_PUBKEY=$(solana-keygen pubkey "$CALCULATION_TMP/smoke.json")
PROGRAM_PUBKEY=$(solana-keygen pubkey "$CALCULATION_TMP/program.json")
PAYER_PUBKEY=$(solana-keygen pubkey "$CALCULATION_TMP/payer.json")
# Check a private block to avoid stale/interrupted validators. PID and identity
# checks below prevent a port-selection race from attaching to another server.
PORT_BASE=$(bash "$SOURCE_ROOT/test/select_calculation_ports.sh" "${CALCULATION_RPC_PORT:-}")
RPC_PORT=$PORT_BASE
RPC_URL=http://127.0.0.1:$RPC_PORT
# Give genesis funds to the disposable payer. Bind locally and assign distinct
# RPC/PubSub, faucet, gossip and dynamic ports from the selected private block.
# Capture validator output in the ledger's temporary parent for startup diagnostics.
solana-test-validator --alpenglow --ledger "$CALCULATION_TMP/ledger" --ticks-per-slot 16 --reset                     \
    --bind-address 127.0.0.1 --rpc-port "$RPC_PORT"                                                                  \
    --faucet-port "$((PORT_BASE + 2))"                                                                               \
    --gossip-port "$((PORT_BASE + 3))"                                                                               \
    --dynamic-port-range "$((PORT_BASE + 10))-$((PORT_BASE + 60))"                                                   \
    --mint "$PAYER_PUBKEY"                                                                                           \
    > "$CALCULATION_TMP/validator.log" 2>&1 &
VALIDATOR_PID=$!
# Poll readiness with bounded HTTP calls; a dead child should fail promptly.
READY=0
for ((attempt=1; attempt<=60; ++attempt)); do
    if ! kill -0 "$VALIDATOR_PID" 2>/dev/null; then
        cat "$CALCULATION_TMP/validator.log" >&2
        exit 1
    fi
    # A responding HTTP server is insufficient: require the validator's health result.
    if curl -sf --max-time 2 "$RPC_URL" -H 'Content-Type: application/json'                                          \
        -d '{"jsonrpc":"2.0","id":1,"method":"getHealth"}' |                                                         \
        jq -e '.result == "ok"' >/dev/null 2>/dev/null; then
        if [ -f "$CALCULATION_TMP/ledger/validator-keypair.json" ]; then
            # A stale validator might have won a port race. Compare its reported
            # identity with the keypair generated in our own newly created ledger.
            EXPECTED_IDENTITY=$(solana-keygen pubkey "$CALCULATION_TMP/ledger/validator-keypair.json")
            ACTUAL_IDENTITY=$(curl -sf --max-time 2 "$RPC_URL" -H 'Content-Type: application/json'                   \
                -d '{"jsonrpc":"2.0","id":1,"method":"getIdentity"}' |                                               \
                jq -er '.result.identity')
            # Check the child again in case it exited while the RPC requests ran.
            if [ "$ACTUAL_IDENTITY" = "$EXPECTED_IDENTITY" ] && kill -0 "$VALIDATOR_PID" 2>/dev/null; then
                READY=1
                break
            fi
        fi
    fi
    sleep 1
done
if [ "$READY" != 1 ]; then
    # Preserve the useful startup reason in the calling test log before cleanup.
    cat "$CALCULATION_TMP/validator.log" >&2
    echo "Validator did not become ready" >&2
    exit 1
fi
# Wait for finalized deployments before using them. Send buffer writes through
# this validator's RPC rather than relying on TPU discovery during local startup.
solana --url "$RPC_URL" --keypair "$CALCULATION_TMP/payer.json" program deploy                                       \
    --program-id "$CALCULATION_TMP/smoke.json" "$CALCULATION_TMP/smoke.so"                                           \
    --buffer "$CALCULATION_TMP/smoke-buffer.json" --use-rpc --commitment finalized
solana --url "$RPC_URL" --keypair "$CALCULATION_TMP/payer.json" program deploy                                       \
    --program-id "$CALCULATION_TMP/program.json" "$CALCULATION_TMP/calculation_tests.so"                             \
    --buffer "$CALCULATION_TMP/calculation-buffer.json" --use-rpc --commitment finalized
# The driver verifies the smoke result first, then executes the calculation
# batches. Pass user controls such as --limit and --verbose through unchanged.
bash "$SOURCE_ROOT/test/run_calculation_vectors.sh" --url "$RPC_URL"                                                 \
    --smoke-program "$SMOKE_PUBKEY" --payer "$CALCULATION_TMP/payer.json" --program "$PROGRAM_PUBKEY" "$@"
