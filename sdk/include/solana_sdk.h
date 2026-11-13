#pragma once
/* Repository-owned adapter; upstream headers remain unmodified. */
#ifndef SOL_SBPFV3
#error "Build with the pinned SBPFv3 compiler configuration"
#endif
#include "../upstream/solana_sdk.h"

/* These two syscalls are used by this project but omitted by the old umbrella
 * header. Addresses are Murmur3-32(name, seed=0), as in solana-define-syscall.
 * Keep legacy input/account ABI: SBPFv3 does not require experimental ABI v2.
 */
static uint64_t sol_get_clock_sysvar(void *ret)
{
    return ((uint64_t (*)(void *))(uint64_t)0xd56b5fe9)(ret);
}

static uint64_t sol_get_rent_sysvar(void *ret)
{
    return ((uint64_t (*)(void *))(uint64_t)0xbf7188f6)(ret);
}
