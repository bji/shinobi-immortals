#pragma once

// This is a unix timestamp
typedef int64_t timestamp_t;

// This is a commission: a fraction of staking rewards, with 0 representing 0% commission and 65535 (0xFFFF), the
// largest value that can be stored, representing 5% commission.  So no commission can ever be more than 5%.  A value
// of c is the fraction c / COMMISSION_FULL_SCALE (65535 * 20) of rewards; 1% is 13107.  See util_calculations.c.
typedef uint16_t commission_t;

// 32 bytes of a SHA-256 value
typedef struct
{
    uint8_t x[32];
} sha256_t;

// This is a 64 bit seed value used to make the SHA-256 of an entry unguessable
typedef uint64_t salt_t;

// Macro to assist in array len calculations
#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))

// This is a Program Derived Address.  The seed prefix which was used to derive the address is not included here
// as it is typically a constant specific to the context in which this program derived address is used.  The
// bump seed however is included since it is unique to the instance of the address.
typedef struct { SolPubkey address; uint8_t bump_seed; } ProgramDerivedAddress;
