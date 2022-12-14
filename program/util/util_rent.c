#pragma once

// Data structure stored in the Rent sysvar
typedef struct
{
    uint64_t lamports_per_byte_year;

    uint8_t exemption_threshold[8];

    uint8_t burn_percent;

} Rent;

// Function missing from solana_sdk.h
extern uint64_t sol_get_rent_sysvar(void *ret);


static uint64_t get_rent_exempt_minimum(uint64_t account_size)
{
    // Get the rent sysvar value
    Rent rent;
    uint64_t result = sol_get_rent_sysvar(&rent);

    // Unfortunately the exemption threshold is in f64 format.  This makes it super difficult to work with since
    // BPF doesn't have floating point instructions.  So do manual computation using the bits of the floating
    // point value.
    uint64_t u = * (uint64_t *) rent.exemption_threshold;
    uint64_t exp = ((u >> 52) & 0x7FF);

    if (result || // sol_get_rent_sysvar failed, so u and exp are bogus
        (u & 0x8000000000000000ul) || // negative exemption_threshold
        ((exp == 0) || (exp == 0x7FF))) { // subnormal values
        // Unsupported and basically nonsensical rent exemption threshold.  Just use some hopefully sane default based
        // on historical values that were true for 2021/2022: lamports_per_byte_year = 3480, exemption_threshold = 2
        // years
        return (account_size + 128) * 3480 * 2;
    }

    // 128 bytes are added for account overhead
    uint64_t min = (account_size + 128) * rent.lamports_per_byte_year;

    if (exp >= 1023) {
        min *= (1 << (exp - 1023));
    }
    else {
        min /= (1 << (1023 - exp));
    }

    // Reduce fraction to 10 bits, to avoid overflow.  Keep track of whether or not to round up.
    uint64_t fraction = u & 0x000FFFFFFFFFFFFFul;
    bool round_up = (fraction & 0x3FFFFFFFFFFul);

    fraction >>= 42;
    if (round_up) {
        fraction += 1;
    }

    fraction *= min;
    fraction /= 0x3FF;

    return min + fraction;
}
