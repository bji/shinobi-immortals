/* Calculation integration-test program. Built for BPF, invoked by transactions.
 * See test/README.txt. There is deliberately no host-side main().
 * Wire format: count:u8, then count records of:
 *   op:u8, initial_overflow:u8, expected_failure:u8, expected_overflow:u8,
 *   args[4]:u64 little endian, expected_value:u64 little endian.
 * Return data: count:u8, then (status:u8, overflow:u8, actual:u64) per record.
 * Status: 0 PASS, 1 XFAIL, 2 FAIL, 3 XPASS.
 */
#include "solana_sdk.h"
#include "../program/util/util_calculations.c"

#define MAX_CASES 16
#define CASE_SIZE 44

static uint64_t read_u64(const uint8_t *p)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) {
        value |= ((uint64_t) p[i]) << (8 * i);
    }
    return value;
}

uint64_t entrypoint(const uint8_t *input)
{
    SolAccountInfo accounts[1];
    SolParameters params = { .ka = accounts };

    if (!sol_deserialize(input, &params, 1) || (params.data_len < 1)) {
        return 1;
    }

    unsigned count = params.data[0];

    if (!count || (count > MAX_CASES) || (params.data_len != (1 + (count * CASE_SIZE)))) {
        return 2;
    }

    uint8_t results[1 + (MAX_CASES * 10)];
    results[0] = count;

    for (unsigned i = 0; i < count; ++i) {
        const uint8_t *p = params.data + 1 + (i * CASE_SIZE);

        if ((p[1] > 1) || (p[2] > 1) || (p[3] > 1)) {
            return 3;
        }

        uint8_t operation = p[0];
        bool overflow = p[1];
        bool expected_failure = p[2];
        bool expected_overflow = p[3];
        uint64_t a = read_u64(p + 4);
        uint64_t b = read_u64(p + 12);
        uint64_t c = read_u64(p + 20);
        uint64_t d = read_u64(p + 28);
        uint64_t expected_result = read_u64(p + 36);
        uint64_t actual_result;

        switch (operation) {
        case 0:
            actual_result = checked_add(a, b, &overflow);
            break;

        case 1:
            actual_result = checked_multiply(a, b, &overflow);
            break;

        case 2:
            if (a && (b < c)) {
                return 4;
            }
            actual_result = compute_price(a, b, c, d);
            break;

        case 3:
            if (!a) {
                return 4;
            }
            actual_result = compute_minimum_bid(a, b, c, d);
            break;

        case 4:
            actual_result = compute_effective_stake_earnings(a, b);
            break;

        case 5:
            if (c > 65535) {
                return 4;
            }
            actual_result = compute_commission(a, b, c);
            break;

        case 6:
            // a packs two uint32_t inputs: level_1_credits in the low 32 bits and expected_annual_reward_bps in the
            // high 32 bits, because each case carries only four operands
            if (b > UINT32_MAX) {
                return 4;
            }
            actual_result = compute_level_credits((uint32_t) a, b, (uint32_t) (a >> 32), c,
                                                  d ? d : LEVEL_CREDIT_WINDOWS_PER_YEAR, &overflow);
            break;

        case 7:
            if ((a > UINT32_MAX) || (b > 8)) {
                return 4;
            }
            actual_result = compute_level_requirement(a, b);
            break;

        case 8:
            if (!c || (b > c)) {
                return 4;
            }
            actual_result = multiply_fraction(a, b, c);
            break;

        case 9:
            // a is the timestamp, b the recorded window, c the window length (0 selects the built-in length)
            actual_result = compute_level_credit_window(a, b, c ? c : LEVEL_CREDIT_WINDOW_SECONDS);
            break;

        case 10:
            // a packs level_1_credits (low 32 bits) and expected_annual_reward_bps (high 32 bits) as for case 6;
            // b is ki_factor, c the target credits, d the windows per year (0 selects the built-in value)
            if (b > UINT32_MAX) {
                return 4;
            }
            actual_result = compute_level_credits_ki_needed((uint32_t) a, b, (uint32_t) (a >> 32), c,
                                                            d ? d : LEVEL_CREDIT_WINDOWS_PER_YEAR);
            break;

        case 16:
            if ((a > 60000000000ul) || (b > UINT32_MAX)) {
                return 4;
            }
            actual_result = compute_ki_harvest(a, b);
            break;

        case 17:
            if ((a > b) || (c > UINT32_MAX)) {
                return 4;
            }
            actual_result = compute_ki_harvest(compute_effective_stake_earnings(a, b), c);
            break;

        default:
            return 5;
        }

        /* Level-credit conversion rejects the value when overflow is set. */
        bool matches =
            ((overflow == expected_overflow) &&
             (((operation == 6) && overflow) ||
              (actual_result == expected_result)));

        unsigned status = expected_failure ? (matches ? 3 : 1) : (matches ? 0 : 2);

        results[1 + (i * 10)] = status;
        results[2 + (i * 10)] = overflow;

        for (unsigned j = 0; j < 8; ++j) {
            results[3 + (i * 10) + j] = (uint8_t)(actual_result >> (8 * j));
        }

        switch (status) {
        case 0:
            sol_log("PASS");
            break;

        case 1:
            sol_log("XFAIL: currently expected to fail");
            break;

        case 2:
            sol_log("FAIL");
            break;

        default:
            sol_log("XPASS: review expected-failure annotation");
            break;
        }
    }

    /* Return success so every completed batch lands, including FAIL/XPASS.
     * The driver reads execution return data and fails the suite on those statuses.
     * Runtime errors are separately detected from transaction metadata.
     */
    sol_set_return_data(results, 1 + (count * 10));

    return 0;
}
