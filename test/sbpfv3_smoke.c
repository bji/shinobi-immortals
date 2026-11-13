/* Minimal runtime proof of a correct build system for SBPFv3 programs, separate from the production program. */
#include "solana_sdk.h"
#include "inc/clock.h"

uint64_t entrypoint(const uint8_t *input)
{
    SolAccountInfo accounts[1];
    SolParameters params = { .ka = accounts };

    if (!sol_deserialize(input, &params, 1) || (params.data_len != 1) || (params.data[0] != 42)) {
        return 1;
    }

    Clock clock;
    uint64_t rent[3]; /* Enough aligned storage for the serialized Rent sysvar. */

    if (sol_get_clock_sysvar(&clock) || sol_get_rent_sysvar(rent)) {
        return 2;
    }

    const uint8_t expected_hash[32] = {
        0x68, 0x48, 0x88, 0xc0, 0xeb, 0xb1, 0x7f, 0x37,
        0x42, 0x98, 0xb6, 0x5e, 0xe2, 0x80, 0x75, 0x26,
        0xc0, 0x66, 0x09, 0x4c, 0x70, 0x1b, 0xcc, 0x7e,
        0xbb, 0xe1, 0xc1, 0x09, 0x5f, 0x49, 0x4f, 0xc1
    };
    uint8_t hash[32];
    SolBytes bytes = { .addr = params.data, .len = 1 };

    if (sol_sha256(&bytes, 1, hash) || sol_memcmp(hash, expected_hash, 32)) {
        return 3;
    }

    sol_log("SBPFv3 C smoke test: PASS");

    const uint8_t answer[] = { 42 };

    sol_set_return_data(answer, sizeof(answer));

    uint8_t returned[1];
    SolPubkey program;

    if ((sol_get_return_data(returned, sizeof(returned), &program) != 1) || (returned[0] != 42) ||
        !SolPubkey_same(&program, params.program_id)) {
        return 4;
    }

    return 0;
}
