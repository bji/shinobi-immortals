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
# evaluate the documented rational curve, in whole lamports.
# For start S, end E and elapsed t, the reference is
# E + (S-E)*(100-t)/(101*(1+t)); dc's integer division supplies the floor.
for ((elapsed=0; elapsed<=100; ++elapsed)); do
    expected=$(dc_calc "100000000000000 1000000000 - 100 $elapsed - * 101 1 $elapsed + * / 1000000000 + p")
    emit "price/curve/$elapsed" 2 100 100000000000000 1000000000 "$elapsed" "$expected"
done
# Long durations with the maximum start price (current_issues.txt issue 12).  For a 100,000 SOL start and a 1 SOL
# floor, the product 100*delta*elapsed exceeds 2^64 from elapsed second 1845 on (from second 1844693 on in the
# earlier thousand-lamport arithmetic); compute_price used to overflow there and fall back to the floor (once at
# 1/1000 of it).  It must instead follow the curve for the whole duration.
# Over a 365 day duration, elapsed times that are multiples of 1/100 of the duration make the time divisions exact,
# so the same rational reference as above applies, with t = 100*elapsed/duration.  Points across the curve, from
# well past the overflow thresholds to near the end, are checked.
PRICE_LONG_DURATION=$((365*24*60*60))
for t in 1 5 6 10 25 50 75 90 99; do
    elapsed=$((PRICE_LONG_DURATION / 100 * t))
    expected=$(dc_calc "100000000000000 1000000000 - 100 $t - * 101 1 $t + * / 1000000000 + p")
    emit "price/long-duration/t$t" 2 "$PRICE_LONG_DURATION" 100000000000000 1000000000 "$elapsed" "$expected"
done
# Elapsed times around the overflow thresholds, durations up to the uint32_t maximum, and prices that are not whole
# multiples of anything do not divide evenly.  For these the reference applies each of the curve's floors
# explicitly, in dc's arbitrary precision (so no intermediate value can overflow), in whole lamports, with d = S-E:
#   E + floor((101*d - floor(100*d*t/T) - d) / (floor(10100*t/T) + 101))
function price_floor_reference ()
{
    local T=$1 S=$2 E=$3 t=$4
    dc_calc "$S $E - sd 101 ld * 100 ld * $t * $T / - ld - 10100 $t * $T / 101 + / $E + p"
}
for case in "$PRICE_LONG_DURATION 1844" "$PRICE_LONG_DURATION 1845" "$PRICE_LONG_DURATION 1844692"                             "$PRICE_LONG_DURATION 1844693" "$PRICE_LONG_DURATION 1844694"                                                        "$PRICE_LONG_DURATION $((PRICE_LONG_DURATION - 1))" "4294967295 1844693" "4294967295 2147483647"                      "4294967295 4294967294"; do
    read -r duration elapsed <<< "$case"
    expected=$(price_floor_reference "$duration" 100000000000000 1000000000 "$elapsed")
    emit "price/long-duration/T$duration-elapsed$elapsed" 2 "$duration" 100000000000000 1000000000 "$elapsed" "$expected"
done
# Prices that are not multiples of 1000 lamports are computed to the lamport, and never fall below the floor.  (The
# earlier thousand-lamport arithmetic rounded the floor down, so that an Immortal could sell for up to 999 lamports
# below its minimum price.)  An equal start and end price must return exactly that price at any time, and a falling
# curve between two non-aligned prices must follow the reference, ending one second before the end at or above the
# floor.
emit price/minimum-rounding 2 100 1001999 1001999 50 1001999
for elapsed in 0 1 50 99; do
    expected=$(price_floor_reference 100 99999123456789 1000000999 "$elapsed")
    emit "price/unaligned/$elapsed" 2 100 99999123456789 1000000999 "$elapsed" "$expected"
done
for elapsed in 1 1844 1845 31535999; do
    expected=$(price_floor_reference "$PRICE_LONG_DURATION" 99999123456789 1000000999 "$elapsed")
    emit "price/unaligned/long-$elapsed" 2 "$PRICE_LONG_DURATION" 99999123456789 1000000999 "$elapsed" "$expected"
done
# The largest start price that admin_create_block accepts, 100,000,000 SOL, must not overflow at any point on the
# curve.  (101 times the price drop, the largest intermediate value, would overflow above about 182,000,000 SOL.)
# Points on a 365 day curve at exact 1/100 steps use the rational reference; points that do not divide evenly,
# including on the longest possible (uint32_t maximum) duration and with a floor that is not a multiple of 1000
# lamports, use the explicit floor reference.
PRICE_MAX_START=100000000000000000
for t in 0 1 6 50 99; do
    elapsed=$((PRICE_LONG_DURATION / 100 * t))
    expected=$(dc_calc "$PRICE_MAX_START 1000000000 - 100 $t - * 101 1 $t + * / 1000000000 + p")
    emit "price/max-start/t$t" 2 "$PRICE_LONG_DURATION" "$PRICE_MAX_START" 1000000000 "$elapsed" "$expected"
done
for case in "$PRICE_LONG_DURATION 1 1000000000" "$PRICE_LONG_DURATION 2 1000000000"                               \
            "$PRICE_LONG_DURATION 1844693 1000000999" "$PRICE_LONG_DURATION $((PRICE_LONG_DURATION - 1)) 1000000999"  \
            "4294967295 1 1000000000" "4294967295 2147483647 1000000999" "4294967295 4294967294 1000000999"; do
    read -r duration elapsed floor <<< "$case"
    expected=$(price_floor_reference "$duration" "$PRICE_MAX_START" "$floor" "$elapsed")
    emit "price/max-start/T$duration-elapsed$elapsed-floor$floor" 2 "$duration" "$PRICE_MAX_START" "$floor"           \
         "$elapsed" "$expected"
done

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
# The multiplier is K*(K+x)/(K*K+K*x+x*x) with K = 50 SOL.  The program measures
# x and K in the finest unit that keeps its arithmetic within uint64_t: units of
# 100 lamports, made ten times larger for as long as x exceeds 2,000,000,000
# units.  Since x is rounded down to a whole number of those units, the
# reference uses the same units; dc then evaluates the full product and
# quotient directly, without the program's overflow-free division.
function ki_reference ()
{
    local raw=$1 stake=$2 x k=500000000
    # x in units of 100 lamports; this fits bash arithmetic even for UINT64_MAX
    x=$(dc_calc "$stake 100 / p")
    while [ "$x" -gt 2000000000 ]; do
        x=$((x / 10))
        k=$((k / 10))
    done
    dc_calc "$raw $k $k $x + * * $k $k * $k $x * + $x $x * + / p"
}

# The stakes include the boundaries at which the program's unit grows: 200 SOL (2,000,000,000 units of 100 lamports),
# 2,000 SOL, and 20,000 SOL, with values just below, at, and above each, and values that differ only below the
# unit in use.
for stake in 0 1 99 100 999999999 1000000000 10000000000 20000000000 50000000000 100000000000                        \
             199999999999 200000000000 200000000099 200000000100 200000001000                                        \
             1999999999999 2000000000000 2000000000999 2000000001000 2000000010000                                   \
             20000000000000 20000000009999 20000000010000                                                            \
             250000000000 500000000000                                                                               \
             1000000000000 1000999999999 2500000000000 5000000000000                                                 \
             10000000000000 25000000000000                                                                           \
             50000000000000 100000000000000 106666000000000 500000000000000                                          \
             999999999999999999 1000000000000000000 1000000000999999999                                              \
             1000000001000000000 1000000009000000000 1000000010000000000 "$MAX"; do
    for raw in 0 1 10 1000 200000 1844674407370955161 "$MAX"; do
        # Apply K*(K+x)/(K*K+K*x+x*x), K=50 SOL.
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

# Commission is floor((current-baseline)*rate/1310700): a rate of 65535 is 5%, so 100% would be 65535 * 20 = 1310700.
# Start with the zero-delta, zero-rate and maximum-rate endpoints before sampling bounded random earnings.
emit commission/no-earnings 5 1000000 1000000 65535 0 0
emit commission/zero-rate 5 2000000 1000000 0 0 0
emit commission/max-rate 5 2000000 1000000 65535 0 50000
emit commission/one-percent 5 2000000 1000000 13107 0 10000
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
    expected=$(dc_calc "$reward $rate * 1310700 / p")
    emit "commission/normal/$i" 5 "$current" "$baseline" "$rate" 0 "$expected"
done
# Rewards large enough that reward * rate exceeds 2^64 (above about 281,000 SOL at the
# maximum rate) must still give the exact result: the floor of reward * rate / 1310700,
# which is 5% of the reward at the maximum rate.  A balance decrease earns none.
emit commission/large-reward 5 300000000000000 0 65535 0 15000000000000
emit commission/large-reward-rate 5 300000000000000 0 6553 0 $(dc_calc "300000000000000 6553 * 1310700 / p")
emit commission/max-reward-max-rate 5 "$MAX" 0 65535 0 $(dc_calc "$MAX 65535 * 1310700 / p")
emit commission/max-reward-max-minus-one 5 "$MAX" 0 65534 0 $(dc_calc "$MAX 65534 * 1310700 / p")
emit commission/max-reward-rate-one 5 "$MAX" 0 1 0 $(dc_calc "$MAX 1310700 / p")
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
            # B = factor*K*(bps/10000)/N whole Ki = factor*bps/(200*N) for K = 50 SOL, with N the default 73
            # windows per year (5 day windows).  Sample around B and 5B, then large contributions, including the
            # largest k for which 40*N*k fits uint64_t and the smallest for which it does not.
            normal=$(dc_calc "$factor $bps * 14600 / p")
            five_normal=$(dc_calc "$normal 5 * p")
            for contribution in 0 1 "$normal" "$five_normal" 1000000000 6317378107434777 6317378107434778; do
                # P(k) = 240*L*k / (factor*bps + 40*N*k); both terms are non-negative, so the denominator exceeds
                # uint64_t exactly when either the product or the sum overflows.
                denominator=$(dc_calc "$factor $bps * 2920 $contribution * + p")
                if [ "$(dc_calc "$MAX $denominator - [1 sF] sz 0 sF 0 >z lF p")" = 1 ]; then
                    emit "level/overflow/$first_cost/$factor/$bps/$contribution" 6 "$packed" "$factor"         \
                         "$contribution" 0 0 1
                else
                    expected=$(dc_calc "$total 240 * $contribution * $denominator / p")
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
    expected=$(dc_calc "$total 240 * 1000 * 1000 490 * 40 $windows * 1000 * + / p")
    emit "level/windows-per-year/$windows" 6 2104533975140 1000 1000 "$windows" "$expected"
done

# Ki needed for a window's curve to reach a target number of credits, used to avoid burning more Ki than is needed
# to reach the maximum level.  With A = 240*L, C = factor*bps and D = 40*N, P(k) = floor(A*k / (C + D*k)), and the
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
                a=$(dc_calc "$total 240 * p")
                d=$(dc_calc "$windows 40 * p")
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
# returning whole Ki. Include the 50-SOL ceiling, fractional SOL boundaries,
# and the conservative upper bound implied by the rounded effective-stake curve.
for effective in 0 1 14435 999999999 1000000000 1000000001 50000000000 60000000000; do
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
