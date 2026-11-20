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
# The sale price curve (compute_price in program/util/util_calculations.c), with steepness k = 1069
# (PRICE_CURVE_STEEPNESS), for duration T, start price S, floor E and elapsed seconds t, before the end:
#   E + floor(floor((S - E) * T / (kt + T)) * k(T - t) / ((k + 1)T))
# evaluated independently in dc's arbitrary precision, so no intermediate value can overflow; dc's integer division
# of non-negative values supplies each floor.
PRICE_STEEPNESS=1069
function price_reference ()
{
    local T=$1 S=$2 E=$3 t=$4 k=$PRICE_STEEPNESS
    dc_calc "$S $E - $T * $k $t * $T + / $k $T $t - * * $k 1 + $T * / $E + p"
}
# A 7 day sale from 1000 SOL down to 0.1 SOL: the start (just below 1000 SOL), the first seconds and minutes, the
# first hours in which most of the drop happens, and through the days in which the price settles onto the floor, up
# to one second before the end.
PRICE_WEEK=$((7*24*60*60))
for elapsed in 0 1 60 600 3600 5040 7200 21600 86400 $((PRICE_WEEK / 2)) $((PRICE_WEEK - 3600)) \
               $((PRICE_WEEK - 1)); do
    expected=$(price_reference "$PRICE_WEEK" 1000000000000 100000000 "$elapsed")
    emit "price/week/$elapsed" 2 "$PRICE_WEEK" 1000000000000 100000000 "$elapsed" "$expected"
done
# The curve is designed so that the price falls to 1/10 of the way from the floor to the start price after 1/120 of
# the sale: 2 hours of a 10 day sale, and 1 hour of a 5 day sale.  Check that the reference meets that to within
# 0.01% of the drop, and that the program agrees with the reference there.
for case in "$((10*24*60*60)) 7200" "$((5*24*60*60)) 3600"; do
    read -r duration elapsed <<< "$case"
    expected=$(price_reference "$duration" 1000000000000 100000000 "$elapsed")
    tenth=$(dc_calc "1000000000000 100000000 - 10 / 100000000 + p")
    tolerance=$(dc_calc "1000000000000 100000000 - 10000 / p")
    # These values are far below 2^63, so the shell's own arithmetic can compare them
    difference=$((expected - tenth))
    if [ $difference -lt 0 ]; then
        difference=$((-difference))
    fi
    if [ $difference -gt $tolerance ]; then
        echo "price/tenth/T$duration: price $expected after $elapsed seconds is not within $tolerance of $tenth" >&2
        exit 1
    fi
    emit "price/tenth/T$duration" 2 "$duration" 1000000000000 100000000 "$elapsed" "$expected"
done
# A short sale still runs from just below the start price to the floor
for elapsed in 0 1 50 99; do
    expected=$(price_reference 100 1000000000000 100000000 "$elapsed")
    emit "price/short/$elapsed" 2 100 1000000000000 100000000 "$elapsed" "$expected"
done
# Prices that are not multiples of anything are computed to the lamport, and never fall below the floor.  An equal
# start and end price must return exactly that price at any time.
emit price/minimum-rounding 2 100 1001999 1001999 50 1001999
for elapsed in 0 1 50 99; do
    expected=$(price_reference 100 99999123456789 1000000999 "$elapsed")
    emit "price/unaligned/$elapsed" 2 100 99999123456789 1000000999 "$elapsed" "$expected"
done
PRICE_LONG_DURATION=$((365*24*60*60))
for elapsed in 1 1844 1845 31535999; do
    expected=$(price_reference "$PRICE_LONG_DURATION" 99999123456789 1000000999 "$elapsed")
    emit "price/unaligned/long-$elapsed" 2 "$PRICE_LONG_DURATION" 99999123456789 1000000999 "$elapsed" "$expected"
done
# The largest start price that admin_create_block accepts, 100,000,000 SOL, on the longest possible (uint32_t
# maximum) duration, must follow the curve at every point without overflowing, including with a floor that is not a
# multiple of 1000 lamports.
PRICE_MAX_START=100000000000000000
for case in "$PRICE_LONG_DURATION 0 1000000000" "$PRICE_LONG_DURATION 1 1000000000"                                 \
            "$PRICE_LONG_DURATION 1844693 1000000999" "$PRICE_LONG_DURATION $((PRICE_LONG_DURATION - 1)) 1000000999"  \
            "4294967295 0 1000000000" "4294967295 1 1000000000" "4294967295 2147483647 1000000999"                    \
            "4294967295 4294967294 1000000999"; do
    read -r duration elapsed floor <<< "$case"
    expected=$(price_reference "$duration" "$PRICE_MAX_START" "$floor" "$elapsed")
    emit "price/max-start/T$duration-elapsed$elapsed-floor$floor" 2 "$duration" "$PRICE_MAX_START" "$floor"           \
         "$elapsed" "$expected"
done

# Minimum bids.  Operand a is the initial minimum bid and b the current highest bid (0 before any bid).  Before any
# bid at or above the initial minimum, the initial minimum applies.  After that the minimum is the current highest
# bid plus 5% of it (MINIMUM_BID_INCREMENT_BPS = 500), rounded up and at least 1 lamport, saturating at UINT64_MAX;
# when the current highest bid is already UINT64_MAX, no bid is possible and the result is 0.
# Prints 1 if $1 < $2 and 0 otherwise, comparing with dc's arbitrary precision.  dc's >r runs macro r when the value
# pushed last ($2) is greater than the one pushed before it ($1).
function dc_less ()
{
    dc_calc "0sr [1sr]sa $1 $2 >a lrp"
}
function bid_reference ()
{
    local initial=$1 current=$2 increment sum
    # The highest bid is already the largest possible: no bid can exceed it
    if [ "$current" = "$MAX" ]; then
        echo 0
        return
    fi
    # No bid at or above the initial minimum yet
    if [ "`dc_less $current $initial`" = "1" ]; then
        echo "$initial"
        return
    fi
    # Round up: (current * 500 + 9999) / 10000, and at least 1
    increment=$(dc_calc "$current 500 * 9999 + 10000 / p")
    if [ "$increment" = "0" ]; then
        increment=1
    fi
    # Saturate at UINT64_MAX
    sum=$(dc_calc "$current $increment + p")
    if [ "`dc_less $MAX $sum`" = "1" ]; then
        echo "$MAX"
    else
        echo "$sum"
    fi
}
# 17568327689247192014 is the largest bid whose minimum next bid (exactly UINT64_MAX) does not saturate
for case in "1000000 0" "1000000 999999" "1000000 1000000" "1000000 1000001" "100 100" "0 0" "0 1" "1 1"             \
            "1 19" "1 20" "1 21" "1000000000 1000000000" "20000000000 20000000000" "1 9999" "1 10000" "1 10001"       \
            "1 18446744073709551614" "1 17568327689247192013" "1 17568327689247192014" "1 17568327689247192015"       \
            "1 $MAX" "$MAX 0" "$MAX $MAX" "1 9223372036854775807"; do
    read -r initial current <<< "$case"
    emit "bid/$initial/$current" 3 "$initial" "$current" 0 0 "$(bid_reference "$initial" "$current")"
done
for ((i=0; i<64; ++i)); do
    # Random current bids across the whole range, with a random initial minimum below 10^12
    current=${random_values[random_index]}
    random_index=$((random_index + 1))
    initial=$(dc_calc "${random_values[random_index]} 1000000000000 % p")
    random_index=$((random_index + 1))
    emit "bid/random/$i" 3 "$initial" "$current" 0 0 "$(bid_reference "$initial" "$current")"
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

# Ki harvest retention: the fraction, of UINT32_MAX, of a harvest that an entry retains after a life span of n
# whole days, with a per-day reduction of r (also a fraction of UINT32_MAX).  The specified computation is repeated
# squaring of q = UINT32_MAX - r, starting from the least significant bit of n, with every product of two such
# fractions rounded down to a whole fraction of UINT32_MAX.  This reference performs that same specified sequence of
# roundings, entirely in dc, so it does not share any code with the program.  (The program stops early once the
# result is 0; carrying on, as this does, gives the same result, since 0 stays 0.)
function retention_reference ()
{
    local reduction=$1 days=$2
    # The reduction is a uint32_t, so Bash arithmetic can compute the base
    local base=$((4294967295 - reduction))
    # dc registers: r is the result, b the base, n the remaining exponent.  Macro m multiplies the result by the
    # base; macro L processes one bit of the exponent and repeats while any bits remain.  "X Y <L" runs L if Y < X.
    dc_calc "4294967295 sr $base sb $days sn
             [lr lb * 4294967295 / sr]sm
             [ln 2 % 1 =m lb lb * 4294967295 / sb ln 2 / sn ln 0 <L]sL
             ln 0 <L
             lr p"
}

# Small, typical and extreme reductions (603528 is about 5% per year), over life spans around bit boundaries,
# typical lifetimes (a year, ten years and a century), and the largest possible.
for reduction in 0 1 603528 4294967 2147483647 2147483648 4294967294 4294967295; do
    for days in 0 1 2 3 4 7 8 365 3650 36500 65535 65536 "$MAX"; do
        expected=$(retention_reference "$reduction" "$days")
        emit "harvest/retention/$reduction/$days" 18 "$reduction" "$days" 0 0 "$expected"
    done
done
# Independent random reductions (mostly small, as they would be in practice) and life spans of up to about 270 years
for ((i=0; i<64; ++i)); do
    reduction=$(dc_calc "${random_values[i+192]} 10000000 % p")
    days=$(dc_calc "${random_values[i+256]} 100000 % p")
    expected=$(retention_reference "$reduction" "$days")
    emit "harvest/retention/random/$i" 18 "$reduction" "$days" 0 0 "$expected"
done

# Retained harvest: effective earnings reduced to the retention (a retention above UINT32_MAX counts as UINT32_MAX),
# rounded down to whole lamports, then converted to whole Ki at ki_factor.
for effective in 0 1 999999999 1000000000 50000000000 60000000000; do
    for factor in 1 70000 4294967295; do
        for retention in 0 1 4080218930 4294967294 4294967295 4294967296 "$MAX"; do
            # The retention, capped at UINT32_MAX: register x holds it, and macro c caps it if UINT32_MAX < x
            capped=$(dc_calc "$retention sx [4294967295 sx]sc lx 4294967295 <c lx p")
            retained=$(dc_calc "$effective $capped * 4294967295 / p")
            expected=$(dc_calc "$retained $factor * 1000000000 / p")
            emit "harvest/retained/$effective/$factor/$retention" 19 "$effective" "$factor" "$retention" 0 "$expected"
        done
    done
done
