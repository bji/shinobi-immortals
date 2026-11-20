#!/bin/bash
# Submit signed transactions and decode BPF results without host floating point.
set -euo pipefail
# Keep numeric byte dumps and text comparisons independent of locale settings.
export LC_ALL=C
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# The parent runner supplies RPC, fee payer and deployed program addresses;
# the remaining options control smoke verification, output volume and case count.
URL= PAYER= PROGRAM= SMOKE_PROGRAM= LIMIT= VERBOSE=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --url) URL=${2:?}; shift 2 ;;
        --payer) PAYER=${2:?}; shift 2 ;;
        --program) PROGRAM=${2:?}; shift 2 ;;
        --smoke-program) SMOKE_PROGRAM=${2:?}; shift 2 ;;
        --limit) LIMIT=${2:?}; shift 2 ;;
        --verbose) VERBOSE=1; shift ;;
        *) echo "Unknown argument: $1" >&2; exit 1 ;;
    esac
done
: "${URL:?--url required}" "${PAYER:?--payer required}" "${PROGRAM:?--program required}"
# Bound --limit's length before using it in Bash's signed integer comparisons.
if [ -n "$LIMIT" ] && ! [[ "$LIMIT" =~ ^[1-9][0-9]{0,6}$ ]]; then
    echo '--limit must be a positive integer of at most seven digits' >&2
    exit 1
fi
# Share scratch files between functions; remove signed transactions and keys' references on exit.
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

function rpc ()
{
    local method=$1 params=$2
    # --arg escapes the method as text; --argjson preserves the parameter array.
    jq -cn --arg method "$method" --argjson params "$params"                                                         \
        '{jsonrpc:"2.0",id:1,method:$method,params:$params}' > "$WORK/request.json" || return 1
    # Bound each HTTP request and submit the exact JSON bytes stored above.
    curl -sf --max-time 5 "$URL" -H 'Content-Type: application/json'                                                 \
        --data-binary "@$WORK/request.json" > "$WORK/rpc.json" || return 1
    # A successful HTTP status can still carry an RPC error or malformed envelope.
    if ! jq -e 'type == "object" and .error == null and has("result")' "$WORK/rpc.json" >/dev/null; then
        cat "$WORK/rpc.json" >&2
        return 1
    fi
    # Preserve JSON null here: getTransaction returns it while execution is pending.
    jq -c .result "$WORK/rpc.json"
}

function invoke ()
{
    local program=$1 encoded signature params transaction attempt ready=0
    # Keep binary wire transactions in files, never in shell variables.
    # Encode the instruction, attach a recent blockhash, then sign with the disposable payer.
    solxact encode < "$WORK/instruction.txt" > "$WORK/encoded" || return 1
    solxact hash "$URL" < "$WORK/encoded" > "$WORK/hashed" || return 1
    solxact sign "$PAYER" < "$WORK/hashed" > "$WORK/signed" || return 1
    # RPC expects the signed wire bytes in base64; remove the encoder's line wrapping.
    encoded=$(base64 < "$WORK/signed" | tr -d '\n') || return 1
    # Keep preflight enabled, but do not mistake successful submission for execution.
    params=$(jq -cn --arg tx "$encoded" '[ $tx, {encoding:"base64",preflightCommitment:"confirmed"} ]') || return 1
    signature=$(rpc sendTransaction "$params") || return 1
    if ! jq -e 'type == "string" and length > 0' <<< "$signature" >/dev/null; then
        return 1
    fi
    # Query this specific signature at confirmed commitment. All transactions here
    # use the legacy format, so maxSupportedTransactionVersion=0 is sufficient.
    params=$(jq -cn --argjson sig "$signature"                                                                       \
        '[$sig,{encoding:"json",commitment:"confirmed",maxSupportedTransactionVersion:0}]') || return 1
    for ((attempt=0; attempt<30; ++attempt)); do
        transaction=$(rpc getTransaction "$params") || return 1
        # Neither a nonempty RPC response nor a transaction without metadata proves
        # execution. Require an explicit err field before interpreting its value.
        if jq -e 'type == "object" and (.meta | type == "object") and (.meta | has("err"))'                          \
            <<< "$transaction" >/dev/null; then
            ready=1
            break
        fi
        sleep 0.2
    done
    if [ "$ready" != 1 ]; then
        echo "Execution result unavailable: $signature" >&2
        return 1
    fi
    # Save metadata for smoke-log checks and diagnostics after decoding return data.
    printf '%s\n' "$transaction" > "$WORK/transaction.json"
    # Require successful execution, return data from our expected program, and the
    # expected encoding. A runtime failure must never become an arithmetic XFAIL.
    if ! jq -e --arg program "$program" '.meta.err == null and .meta.returnData.programId == $program and
        .meta.returnData.data[1] == "base64" and (.meta.returnData.data[0] | type == "string")'                      \
        "$WORK/transaction.json" >/dev/null; then
        cat "$WORK/transaction.json" >&2
        return 1
    fi
    # Leave raw bytes in a file so zero bytes and full uint64 values survive intact.
    jq -r .meta.returnData.data[0] "$WORK/transaction.json" | base64 -d > "$WORK/results" || return 1
}

if [ -n "$SMOKE_PROGRAM" ]; then
    # Instruction byte 42 requests the smoke test. Its return data must be that
    # same single byte, and its persisted execution log must contain the PASS marker.
    printf 'encoding rust_bincode_fixedint\nfee_payer %s\nprogram %s\nu8 42\n'                                       \
        "$PAYER" "$SMOKE_PROGRAM" > "$WORK/instruction.txt"
    invoke "$SMOKE_PROGRAM"
    # printf's octal escape 052 is decimal 42, with no trailing newline.
    printf '\052' > "$WORK/expected-smoke"
    cmp -s "$WORK/results" "$WORK/expected-smoke"
    jq -e '.meta.logMessages | index("Program log: SBPFv3 C smoke test: PASS") != null'                              \
        "$WORK/transaction.json" >/dev/null
    echo 'PASS: SBPFv3 C smoke transaction (log and return data verified)'
fi

# Materialize vectors once, so batches refer to a fixed deterministic input sequence.
bash "$SCRIPT_DIR/calculation_vectors.sh" > "$WORK/vectors"
mapfile -t vectors < "$WORK/vectors"
total=${#vectors[@]}
if [ -n "$LIMIT" ] && [ "$LIMIT" -lt "$total" ]; then
    total=$LIMIT
fi
# Indices are the BPF status codes: 0 PASS, 1 XFAIL, 2 FAIL, 3 XPASS.
counts=(0 0 0 0)
runtime_failures=0
labels=(PASS XFAIL FAIL XPASS)
for ((start=0; start<total; start+=16)); do
    # The BPF entrypoint accepts at most 16 cases, keeping instruction size bounded.
    count=$((total - start))
    if [ "$count" -gt 16 ]; then
        count=16
    fi
    {
        # Fixed-width Rust encoding packs primitives without C structure padding.
        # Prefix the batch with its one-byte count, then serialize each 44-byte case:
        # four flag/op bytes, four uint64 arguments, and one uint64 expected result.
        printf 'encoding rust_bincode_fixedint\nfee_payer %s\nprogram %s\nu8 %s\n' "$PAYER" "$PROGRAM" "$count"
        for ((i=0; i<count; ++i)); do
            # Names and defect explanations are host diagnostics, not instruction data.
            IFS='|' read -r name defect op sticky xfail overflow a b c d expected <<< "${vectors[start+i]}"
            printf 'u8 %s\nu8 %s\nu8 %s\nu8 %s\nu64 %s\nu64 %s\nu64 %s\nu64 %s\nu64 %s\n'                            \
                "$op" "$sticky" "$xfail" "$overflow" "$a" "$b" "$c" "$d" "$expected"
        done
    } > "$WORK/instruction.txt"
    # Record a failed batch and continue with subsequent batches, rather than
    # losing coverage after a single transport, preflight or runtime error.
    if ! invoke "$PROGRAM"; then
        echo "FAIL: transaction batch $((start / 16))" >&2
        runtime_failures=$((runtime_failures + 1))
        continue
    fi
    # Decode bytes using ordinary whitespace-separated od output. Flatten line
    # breaks before read -a, without depending on a nonportable od row-width option.
    byte_text=$(od -An -v -tu1 "$WORK/results")
    read -r -a bytes <<< "${byte_text//$'\n'/ }"
    # Return-data layout: one count byte, followed by count ten-byte records.
    # Reject truncated/extra data and a count that differs from the submitted batch.
    if [ "${#bytes[@]}" -ne "$((1 + 10 * count))" ] || [ "${bytes[0]:-0}" -ne "$count" ]; then
        echo "FAIL: incomplete BPF results for batch $((start / 16))" >&2
        runtime_failures=$((runtime_failures + 1))
        continue
    fi
    for ((i=0; i<count; ++i)); do
        # A record holds status at +0, overflow at +1, and a little-endian uint64 at +2.
        # The initial count byte makes their absolute offsets 1+10*i, 2+10*i, 3+10*i.
        status=${bytes[1+i*10]}
        overflow=${bytes[2+i*10]}
        if [ "$status" -gt 3 ] || [ "$overflow" -gt 1 ]; then
            echo 'FAIL: invalid BPF test status' >&2
            runtime_failures=$((runtime_failures + 1))
            continue
        fi
        # The pinned execution/build host is Linux x86_64 (little endian).
        # od prints the full unsigned 64-bit value, without Bash/jq rounding.
        actual=$(od -An -v -tu8 -j "$((3 + i*10))" -N8 "$WORK/results" | tr -d ' \n')
        counts[status]=$((counts[status] + 1))
        # Always explain failures/expected failures; --verbose also prints every PASS.
        if [ "$status" -ne 0 ] || [ "$VERBOSE" = 1 ]; then
            IFS='|' read -r name defect op sticky xfail expected_overflow a b c d expected <<< "${vectors[start+i]}"
            detail=
            if [ "$status" = 1 ]; then
                detail=" -- currently expected to fail: $defect"
            fi
            printf '%s: %s%s; actual=%s, overflow=%s; expected=%s, overflow=%s\n'                                    \
                "${labels[status]}" "$name" "$detail" "$actual" "$overflow" "$expected" "$expected_overflow"
        fi
    done
    # Emit occasional progress even when individual passing cases are suppressed.
    if [ "$((start % 256))" = 0 ]; then
        echo "Executed $((start + count))/$total cases through transactions"
    fi
done
printf '%s PASS, %s XFAIL, %s FAIL, %s XPASS; %s transaction/runtime failures\n' "${counts[@]}" "$runtime_failures"
# Expected failures are permitted. An unexpected pass requires reviewing a stale
# defect annotation, so it fails the run just like an unexpected failure does.
[ "${counts[2]}" = 0 ] && [ "${counts[3]}" = 0 ] && [ "$runtime_failures" = 0 ]
