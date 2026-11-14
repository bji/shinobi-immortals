#!/bin/bash
# Emit pipe-delimited reference vectors. Only dc computes wide integer values;
# the production calculations execute exclusively in the BPF test program.
set -euo pipefail
export LC_ALL=C
# Keep these as decimal strings: MAX is UINT64_MAX; MOD is the wrap modulus 2^64.
# Bash arithmetic is used only for small indices/counters, never these wide values.
MAX=18446744073709551615
MOD=18446744073709551616

function dc_calc ()
{
    # Feed standard input rather than relying on dc's nonportable -e option.
    printf '%s\n' "$1" | dc
}

function emit ()
{
    # Arguments describe a case, four operands, and its independently computed result.
    # Optional flags default to ordinary, non-overflowing, non-sticky expectations.
    local name=$1 op=$2 a=$3 b=$4 c=$5 d=$6 expected=$7
    local overflow=${8:-0} sticky=${9:-0} defect=${10:-} xfail=0
    # A named known defect is sent to BPF as an expected failure, not skipped.
    if [ -n "$defect" ]; then
        xfail=1
    fi
    # Preserve wide numbers as text in the eleven-column, pipe-delimited wire source.
    printf '%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s\n'                                                                      \
        "$name" "$defect" "$op" "$sticky" "$xfail" "$overflow" "$a" "$b" "$c" "$d" "$expected"
}

# Fixed-seed 64-bit LCG: reproducible, independent of Bash's signed arithmetic
# and of platform-specific $RANDOM behavior. This is test data, not cryptography.
# The recurrence is state = (state * 6364136223846793005 + 1442695040888963407) mod 2^64.
# dc register r stores state: lr loads it, d duplicates the new state, sr stores
# one copy and p prints the other. Generate enough values for all random groups.
mapfile -t random_values < <(
    {
        echo '2195165876 sr'
        for ((i=0; i<1024; ++i)); do
            echo "lr 6364136223846793005 * 1442695040888963407 + $MOD % d sr p"
        done
    } | dc
)
if [ "${#random_values[@]}" -ne 1024 ]; then
    echo 'Failed to generate deterministic inputs' >&2
    exit 1
fi
# Consume the generated stream in a fixed order so repeated invocations agree.
random_index=0
pair_index=0

function arithmetic_pair ()
{
    local a=$1 b=$2 op label operator output value high sticky overflow
    for op in 0 1; do
        # Operation indices must match the switch in test/unit_tests.c.
        if [ "$op" = 0 ]; then
            label=add
            operator=+
        else
            label=multiply
            operator='*'
        fi
        # dc's p prints without popping. Remove the printed low word before
        # dividing the original full result to obtain its overflow/carry word.
        output=$(dc_calc "$a $b $operator d $MOD % p sL $MOD / p")
        # The first line is result mod 2^64; the second is floor(result / 2^64).
        value=${output%%$'\n'*}
        high=${output#*$'\n'}
        # Test each pair with a clear flag and an already-set flag. Overflow must
        # be sticky: a later operation may set it but must never clear it.
        for sticky in 0 1; do
            overflow=$sticky
            if [ "$high" != 0 ]; then
                overflow=1
            fi
            emit "$label/$pair_index/sticky=$sticky" "$op" "$a" "$b" 0 0 "$value" "$overflow" "$sticky"
        done
    done
    pair_index=$((pair_index + 1))
}

# Cross-product boundaries cover zero, small values, the 32-bit limb boundary,
# the signed/unsigned 64-bit boundary, and the largest unsigned integers.
edges=(0 1 2 4294967295 4294967296 4294967297 9223372036854775807 18446744073709551614 "$MAX")
for a in "${edges[@]}"; do
    for b in "${edges[@]}"; do
        arithmetic_pair "$a" "$b"
    done
done
# Add 128 full-width random pairs and 128 pairs with a 32-bit second operand.
# The latter exercise multiplication cases with only one nonzero high limb.
for ((i=0; i<256; ++i)); do
    a=${random_values[random_index]}
    random_index=$((random_index + 1))
    b=${random_values[random_index]}
    random_index=$((random_index + 1))
    if [ "$i" -ge 128 ]; then
        b=$(dc_calc "$b 4294967296 % p")
    fi
    arithmetic_pair "$a" "$b"
done

# Price endpoints must return the floor immediately for zero duration and at or
# beyond the end time. Equal start/end prices exercise a constant-price curve.
emit price/zero-duration 2 0 1000000 100000 0 100000
emit price/end 2 100 1000000 100000 100 100000
emit price/after-end 2 100 1000000 100000 "$MAX" 100000
emit price/constant 2 100 1000000 1000000 50 1000000
# Duration 100 makes the intermediate time divisions exact. Independently
# evaluate the documented rational curve, then round to 1000 lamports.
# For start S, end E and elapsed t, the reference is
# E + (S-E)*(100-t)/(101*(1+t)); dc's integer division supplies each floor.
for ((elapsed=0; elapsed<=100; ++elapsed)); do
    expected=$(dc_calc "100000000000000 1000000000 - 100 $elapsed - * 101 1 $elapsed + * / 1000000000 + 1000 / 1000 * p")
    emit "price/curve/$elapsed" 2 100 100000000000000 1000000000 "$elapsed" "$expected"
done
# An equal, non-1000-aligned start/end price exposes truncation below the floor.
emit price/minimum-rounding 2 100 1001999 1001999 50 1001999 0 0                                                     \
    '1000-lamport rounding undercuts the minimum'

# Bid boundaries exercise the first bid, the maximum-bid sentinel, time clamping,
# and both overflow fallbacks: adding one eighth and saturating at UINT64_MAX.
emit bid/first 3 100 1000000 0 0 1000000
emit bid/max 3 100 1000000 "$MAX" 0 0
emit bid/start 3 100 1000000 1000000 0 1019900
emit bid/last-second 3 100 1000000 1000000 99 1510000
emit bid/end 3 100 1000000 1000000 100 1510000
emit bid/saturating-overflow 3 100 1 18446744073709551614 0 "$MAX"
expected=$(dc_calc '9223372036854775807 d 8 / + p')
emit bid/overflow-fallback 3 100 1 9223372036854775807 0 "$expected"
# At duration 1000, evaluate all valid elapsed seconds of the published fixed-point
# bid curve: floor(1,000,000 * (floor(1,000,000/(1010-t)) + 101000) / 100000).
for ((elapsed=0; elapsed<1000; ++elapsed)); do
    expected=$(dc_calc "1000000 1000000 1010 $elapsed - / 101000 + * 100000 / p")
    emit "bid/curve/$elapsed" 3 1000 1000000 1000000 "$elapsed" "$expected"
done

# Independently evaluate the total-stake multiplier using dc's arbitrary-size
# integers. Operand a is whole Ki earned; b is total delegated stake in lamports.
# Include sub-SOL rounding boundaries, the old curve's intercept, and UINT64_MAX.
# The reference keeps K=500 and rounds x to ten SOL above one billion SOL.
# This is mathematically equivalent to changing units in the production code;
# dc can safely evaluate the full product and quotient directly.
function ki_reference ()
{
    local raw=$1 stake=$2 stake_sol
    stake_sol=$(dc_calc "$stake 1000000000 / p")
    if [ "$stake_sol" -gt 1000000000 ]; then
        stake_sol=$(dc_calc "$stake_sol 10 / 10 * p")
    fi
    dc_calc "$raw 10 * 500 500 $stake_sol + * * 250000 500 $stake_sol * + $stake_sol d * + / p"
}

for stake in 0 1 999999999 1000000000 10000000000 20000000000 250000000000 500000000000                              \
             1000000000000 1000999999999 2500000000000 5000000000000                                                 \
             10000000000000 25000000000000                                                                           \
             50000000000000 100000000000000 106666000000000 500000000000000                                          \
             999999999999999999 1000000000000000000 1000000000999999999                                              \
             1000000001000000000 1000000009000000000 1000000010000000000 "$MAX"; do
    for raw in 0 1 10 1000 200000 1844674407370955161; do
        # Convert to deciKi first, then apply K*(K+x)/(K*K+K*x+x*x), K=500.
        expected=$(ki_reference "$raw" "$stake")
        emit "Ki/stake/$stake/earnings/$raw" 4 "$raw" "$stake" 0 0 "$expected"
    done
done
# Random wide inputs exercise binary division and large total stakes.
for ((i=0; i<64; ++i)); do
    stake=${random_values[i]}
    raw=$(dc_calc "${random_values[i+64]} 1844674407370955162 % p")
    expected=$(ki_reference "$raw" "$stake")
    emit "Ki/random/$i" 4 "$raw" "$stake" 0 0 "$expected"
done
# Overflow flags are sticky, and deciKi conversion must reject values above u64.
emit Ki/multiplication-overflow 4 "$MAX" 0 0 0 0 1
emit Ki/sticky-overflow 4 1 50000000000000 0 0 0 1 1

# Commission is floor((current-baseline)*rate/65535). Start with the zero-delta,
# zero-rate and full-rate endpoints before sampling bounded random earnings.
emit commission/no-earnings 5 1000000 1000000 65535 0 0
emit commission/zero-rate 5 2000000 1000000 0 0 0
emit commission/full-rate 5 2000000 1000000 65535 0 1000000
for ((i=0; i<128; ++i)); do
    # Bound both balances below 10^12 and sample the entire uint16 commission range.
    reward=$(dc_calc "${random_values[random_index]} 1000000000000 % p")
    random_index=$((random_index + 1))
    baseline=$(dc_calc "${random_values[random_index]} 1000000000000 % p")
    random_index=$((random_index + 1))
    rate=$(dc_calc "${random_values[random_index]} 65536 % p")
    random_index=$((random_index + 1))
    # Use wide reference math before division; the final result fits uint64_t.
    current=$(dc_calc "$baseline $reward + p")
    expected=$(dc_calc "$reward $rate * 65535 / p")
    emit "commission/normal/$i" 5 "$current" "$baseline" "$rate" 0 "$expected"
done
# These references express intended behavior rather than reproducing unsigned
# overflow: full commission equals the reward, and a balance decrease earns none.
emit commission/large-reward 5 300000000000000 0 65535 0 300000000000000 0 0                                         \
    'multiplication overflows before division'
emit commission/decreased-balance 5 99900000000 100000000000 6553 0 0 0 0                                            \
    'unsigned earnings subtraction underflows'
