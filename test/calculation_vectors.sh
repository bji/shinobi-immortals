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
# Long durations with the maximum start price (current_issues.txt issue 12).  For a 100,000 SOL start and a 1 SOL
# floor, the product 100*delta*elapsed (delta in thousands of lamports) exceeds 2^64 from elapsed second 1844693 on;
# compute_price used to overflow there and fall back to the floor (once at 1/1000 of it).  It must instead follow the
# curve for the whole duration.
# Over a 365 day duration, elapsed times that are multiples of 1/100 of the duration make the time divisions exact,
# so the same rational reference as above applies, with t = 100*elapsed/duration.  Points on both sides of the old
# overflow threshold (t = 5 is below it, t = 6 above) and near the end are checked.
PRICE_LONG_DURATION=$((365*24*60*60))
for t in 1 5 6 10 25 50 75 90 99; do
    elapsed=$((PRICE_LONG_DURATION / 100 * t))
    expected=$(dc_calc "100000000000000 1000000000 - 100 $t - * 101 1 $t + * / 1000000000 + 1000 / 1000 * p")
    emit "price/long-duration/t$t" 2 "$PRICE_LONG_DURATION" 100000000000000 1000000000 "$elapsed" "$expected"
done
# Elapsed times around the old overflow threshold, and durations up to the uint32_t maximum, do not divide evenly.
# For these the reference applies each of the curve's floors explicitly, in dc's arbitrary precision (so no
# intermediate value can overflow), with d = (S-E)/1000 and e = E/1000:
#   1000 * (e + floor((101*d - floor(100*d*t/T) - d) / (floor(10100*t/T) + 101)))
function price_floor_reference ()
{
    local T=$1 S=$2 E=$3 t=$4
    dc_calc "$S $E - 1000 / sd $E 1000 / se 101 ld * 100 ld * $t * $T / - ld - 10100 $t * $T / 101 + / le + 1000 * p"
}
for case in "$PRICE_LONG_DURATION 1844692" "$PRICE_LONG_DURATION 1844693" "$PRICE_LONG_DURATION 1844694"           \
            "$PRICE_LONG_DURATION $((PRICE_LONG_DURATION - 1))" "4294967295 1844693" "4294967295 2147483647"          \
            "4294967295 4294967294"; do
    read -r duration elapsed <<< "$case"
    expected=$(price_floor_reference "$duration" 100000000000000 1000000000 "$elapsed")
    emit "price/long-duration/T$duration-elapsed$elapsed" 2 "$duration" 100000000000000 1000000000 "$elapsed" "$expected"
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
# integers. Operand a is earned lamports; b is total delegated stake in lamports.
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
    dc_calc "$raw 500 500 $stake_sol + * * 250000 500 $stake_sol * + $stake_sol d * + / p"
}

for stake in 0 1 999999999 1000000000 10000000000 20000000000 250000000000 500000000000                              \
             1000000000000 1000999999999 2500000000000 5000000000000                                                 \
             10000000000000 25000000000000                                                                           \
             50000000000000 100000000000000 106666000000000 500000000000000                                          \
             999999999999999999 1000000000000000000 1000000000999999999                                              \
             1000000001000000000 1000000009000000000 1000000010000000000 "$MAX"; do
    for raw in 0 1 10 1000 200000 1844674407370955161 "$MAX"; do
        # Apply K*(K+x)/(K*K+K*x+x*x), K=500.
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
# Effective earnings use a bounded ratio without multiplication overflow.
emit Ki/full-width-earnings 4 "$MAX" 0 0 0 "$MAX"
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
emit commission/decreased-balance 5 99900000000 100000000000 6553 0 0
# Stake exactly at the baseline, and the smallest and largest possible decreases, also earn no commission.
emit commission/unchanged-balance 5 100000000000 100000000000 65535 0 0
emit commission/decreased-by-one 5 99999999999 100000000000 65535 0 0
emit commission/decreased-to-zero 5 0 "$MAX" 65535 0 0

# Cumulative level thresholds use the same fixed-point rounding as the metadata
# schedule, independently evaluated with arbitrary precision rather than C code.
for first_cost in 1 100 4294967295; do
    cost=$(dc_calc "$first_cost 10 * p")
    total=0
    emit "level/threshold/$first_cost/0" 7 "$first_cost" 0 0 0 0
    for ((level=1; level<=8; ++level)); do
        total=$(dc_calc "$total $cost + p")
        emit "level/threshold/$first_cost/$level" 7 "$first_cost" "$level" 0 0 "$total"
        cost=$(dc_calc "$cost d 2 / + p")
    done
    for factor in 1 1000 1000000 4294967295; do
        for bps in 1 490 4294967295; do
            # Operand a packs level_1_credits (low 32 bits) with expected_annual_reward_bps (high 32 bits).
            packed=$(dc_calc "$bps 4294967296 * $first_cost + p")
            # B = factor*500*(bps/10000)/N whole Ki = factor*bps/(20*N), with N the default 73 windows per year
            # (5 day windows).  Sample around B and 5B, then large contributions, including ones whose 4*N*k cannot
            # fit uint64_t.
            normal=$(dc_calc "$factor $bps * 1460 / p")
            five_normal=$(dc_calc "$normal 5 * p")
            for contribution in 0 1 "$normal" "$five_normal" 1000000000 63173781074347779; do
                # P(k) = 24*L*k / (factor*bps + 4*N*k); both terms are non-negative, so the denominator exceeds
                # uint64_t exactly when either the product or the sum overflows.
                denominator=$(dc_calc "$factor $bps * 292 $contribution * + p")
                if [ "$(dc_calc "$MAX $denominator - [1 sF] sz 0 sF 0 >z lF p")" = 1 ]; then
                    emit "level/overflow/$first_cost/$factor/$bps/$contribution" 6 "$packed" "$factor"         \
                         "$contribution" 0 0 1
                else
                    expected=$(dc_calc "$total 24 * $contribution * $denominator / p")
                    emit "level/credits/$first_cost/$factor/$bps/$contribution" 6 "$packed" "$factor"          \
                         "$contribution" 0 "$expected"
                fi
            done
        done
    done
done
# 2104533975040 = 490 << 32, and 2104533975140 = (490 << 32) + 100
emit level/zero-first-cost 6 2104533975040 1000 1 0 0 1
emit level/zero-factor 6 2104533975140 0 1 0 0 1
emit level/zero-reward-rate 6 100 1000 1 0 0 1
emit level/scale-overflow 6 2104533975140 1000 "$MAX" 0 0 1
emit level/sticky-overflow 6 2104533975140 1000 1 0 0 1 1

# Exercise the overflow-free product/division with arbitrary full-width operands.
for ((i=0; i<64; ++i)); do
    value=${random_values[i]}
    denominator=${random_values[i+64]}
    numerator=$(dc_calc "${random_values[i+128]} $denominator % p")
    expected=$(dc_calc "$value $numerator * $denominator / p")
    emit "fraction/random/$i" 8 "$value" "$numerator" "$denominator" 0 "$expected"
done
emit fraction/full-width-identity 8 "$MAX" "$MAX" "$MAX" 0 "$MAX"
emit fraction/full-width-half 8 "$MAX" 9223372036854775808 "$MAX" 0 9223372036854775808

# Credit conversion must use the supplied windows per year, not the default constant.
for windows in 1 73 340 365 525600 1000000; do
    # level_1_credits=100 gives 49,257 tenths of a lifetime credit after integer rounding.
    total=0 cost=1000
    for ((i=0; i<8; ++i)); do
        total=$(dc_calc "$total $cost + p")
        cost=$(dc_calc "$cost d 2 / + p")
    done
    # factor=1000, bps=490 (packed with level_1_credits=100 as 2104533975140), and k=1000
    expected=$(dc_calc "$total 24 * 1000 * 1000 490 * 4 $windows * 1000 * + / p")
    emit "level/windows-per-year/$windows" 6 2104533975140 1000 1000 "$windows" "$expected"
done

# Ki needed for a window's curve to reach a target number of credits, used to avoid burning more Ki than is needed
# to reach the maximum level.  With A = 24*L, C = factor*bps and D = 4*N, P(k) = floor(A*k / (C + D*k)), and the
# least k with P(k) >= T is ceil(T*C / (A - T*D)), or unreachable (UINT64_MAX) when A <= T*D.  dc evaluates this
# exactly.  Where the forward calculation fits in 64 bits, also check that P(k) reaches the target and P(k-1) does not,
# so that the result is confirmed to be the smallest sufficient amount.
for first_cost in 1 100 4294967295; do
    # L, the level 9 requirement in tenths of a credit, by the same recurrence as compute_level_requirement
    total=0 cost=$(dc_calc "$first_cost 10 * p")
    for ((i=0; i<8; ++i)); do
        total=$(dc_calc "$total $cost + p")
        cost=$(dc_calc "$cost d 2 / + p")
    done
    for factor in 1 1000 4294967295; do
        for bps in 1 490 4294967295; do
            packed=$(dc_calc "$bps 4294967296 * $first_cost + p")
            c=$(dc_calc "$factor $bps * p")
            for windows in 73 525600; do
                a=$(dc_calc "$total 24 * p")
                d=$(dc_calc "$windows 4 * p")
                # The largest reachable target is the largest integer below A/D
                top=$(dc_calc "$a 1 - $d / p")
                for target in 1 2 $(dc_calc "$top 2 / p") $(dc_calc "$top 1 - p") "$top" $(dc_calc "$top 1 + p") "$MAX"; do
                    # Skip targets that came out as zero or negative for tiny curves (dc prints negatives with '-')
                    case "$target" in 0|-*) continue ;; esac
                    td=$(dc_calc "$target $d * p")
                    # In dc, "X Y !<x" runs macro x unless Y < X; so this prints 1 when T*D >= A (unreachable)
                    unreachable=$(dc_calc "0 sr $a $td [1sr]sx !<x lr p")
                    if [ "$unreachable" = 1 ]; then
                        expected=$MAX
                    else
                        e=$(dc_calc "$a $td - p")
                        expected=$(dc_calc "$target $c * $e + 1 - $e / p")
                        # Results that do not fit below UINT64_MAX are also reported as UINT64_MAX.  So are results
                        # for which the forward calculation overflows (C + D*k >= 2^64): the program would reject a
                        # contribution reaching that window total anyway, so no limit is needed.
                        if [ "$(dc_calc "0 sr $expected $MAX [1sr]sx !>x lr p")" = 1 ] ||
                           [ "$(dc_calc "0 sr $c $d $expected * + $MOD [1sr]sx !>x lr p")" = 1 ]; then
                            expected=$MAX
                        fi
                    fi
                    emit "level/ki-needed/$first_cost/$factor/$bps/$windows/$target" 10 "$packed" "$factor"         \
                        "$target" "$windows" "$expected"
                    # Cross-check against the forward curve where it does not overflow: C + D*k < 2^64
                    if [ "$expected" != "$MAX" ]; then
                        for k in "$expected" $(dc_calc "$expected 1 - p"); do
                            fits=$(dc_calc "0 sr $c $d $k * + $MOD [1sr]sx >x lr p")
                            if [ "$fits" = 1 ] && [ "$(dc_calc "0 sr $k 0 [1sr]sx !>x lr p")" = 1 ]; then
                                credits=$(dc_calc "$a $k * $c $d $k * + / p")
                                emit "level/ki-needed-check/$first_cost/$factor/$bps/$windows/$target/$k" 6         \
                                    "$packed" "$factor" "$k" "$windows" "$credits"
                            fi
                        done
                    fi
                done
            done
        done
    done
done
emit level/ki-needed/zero-target 10 2104533975140 1000 0 73 0
emit level/ki-needed/zero-first-cost 10 2104533975040 1000 1 73 "$MAX"
emit level/ki-needed/zero-factor 10 2104533975140 0 1 73 "$MAX"
emit level/ki-needed/zero-reward-rate 10 100 1000 1 73 "$MAX"

# Level credit windows: timestamp / length, but never earlier than the entry's recorded window, so that a clock
# stepping backwards across a boundary cannot reset the credit curve.  Check both the production 5 day length and
# the 60 second test length, around boundaries and at the extremes.
for length in 432000 60; do
    for timestamp in 0 1 $((length - 1)) "$length" $((length + 1)) $((length * 6 - 1)) $((length * 6)) 1763078400 "$MAX"; do
        window=$(dc_calc "$timestamp $length / p")
        # Nothing recorded yet, and the same window already recorded: the computed window is used
        emit "window/$length/$timestamp/fresh" 9 "$timestamp" 0 "$length" 0 "$window"
        emit "window/$length/$timestamp/same" 9 "$timestamp" "$window" "$length" 0 "$window"
        # A recorded window later than the clock's (the clock stepped backwards): clamp to the recorded window
        for later in 1 1000; do
            recorded=$(dc_calc "$window $later + p")
            if [ "$(dc_calc "$recorded $MOD / p")" = 0 ]; then
                emit "window/$length/$timestamp/backwards/$later" 9 "$timestamp" "$recorded" "$length" 0 "$recorded"
            fi
        done
        # A recorded window earlier than the clock's: the clock's (new) window is used
        if [ "$window" != 0 ]; then
            emit "window/$length/$timestamp/forward" 9 "$timestamp" $(dc_calc "$window 1 - p") "$length" 0 "$window"
        fi
    done
    emit "window/$length/recorded-max" 9 0 "$MAX" "$length" 0 "$MAX"
done

# The second harvest stage applies a uint32_t factor to effective lamports,
# returning whole Ki. Include the 50-SOL example, fractional SOL boundaries,
# and the conservative upper bound implied by the rounded effective-stake curve.
for effective in 0 1 14435 999999999 1000000000 1000000001 50000000000 510000000000; do
    for factor in 0 1 50000 70000 4294967295; do
        expected=$(dc_calc "$effective $factor * 1000000000 / p")
        emit "harvest/convert/$effective/$factor" 16 "$effective" "$factor" 0 0 "$expected"
    done
done

# Test the full production ordering, reducing lamport earnings before applying
# ki_factor. Earnings cannot exceed current delegated stake in a real account.
for stake in 100000000 10000000000 500000000000 5000000000000 1000000009000000000 "$MAX"; do
    epoch_rewards=$(dc_calc "$stake 14435 * 100000000 / p")
    long_rewards=$(dc_calc "$epoch_rewards 500 * p")
    for earnings in 0 1 "$epoch_rewards" "$long_rewards" "$stake"; do
        effective=$(ki_reference "$earnings" "$stake")
        for factor in 1 70000 4294967295; do
            expected=$(dc_calc "$effective $factor * 1000000000 / p")
            emit "harvest/pipeline/$stake/$earnings/$factor" 17 "$earnings" "$stake" "$factor" 0 "$expected"
        done
    done
done
# Independent random valid account balances and reward deltas.
for ((i=0; i<64; ++i)); do
    stake=${random_values[i]}
    earnings=$(dc_calc "${random_values[i+64]} $stake 1 + % p")
    factor=$(dc_calc "${random_values[i+128]} 4294967296 % p")
    effective=$(ki_reference "$earnings" "$stake")
    expected=$(dc_calc "$effective $factor * 1000000000 / p")
    emit "harvest/pipeline/random/$i" 17 "$earnings" "$stake" "$factor" 0 "$expected"
done
