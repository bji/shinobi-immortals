#!/bin/bash

# XXXCODEX: What is this file for?  When is it called?  What does it do in broad terms?

# Linux is the pinned test host. Check both TCP and UDP, including IPv6 wildcard
# listeners; startup also verifies the validator PID and its RPC identity.
set -euo pipefail
# Index occupied port numbers for constant-time lookups when scanning candidate blocks.
declare -A occupied=()
# /proc/net entries store the local address as ADDRESS:HEX_PORT. Ignore the
# heading row and collect both IPv4 and IPv6, for TCP and UDP listeners/bindings.
while read -r _ address _; do
    port=${address##*:}
    # The explicit base-16 prefix also handles port strings with leading zeroes.
    if [[ "$port" =~ ^[0-9A-Fa-f]{4}$ ]]; then
        occupied[$((16#$port))]=1
    fi
done < <(cat /proc/net/tcp /proc/net/tcp6 /proc/net/udp /proc/net/udp6)
if [ -n "${1:-}" ]; then
    # Keep below 65536 after adding 60, and avoid privileged ports below 1024.
    [[ "$1" =~ ^[1-9][0-9]{3,4}$ ]] && [ "$1" -ge 1024 ] && [ "$1" -le 65475 ] ||
        {
            echo 'Invalid validator port block base' >&2
            exit 1
        }
    candidates=("$1")
else
    candidates=()
    # Randomize the first block, then try 100 distinct blocks at 100-port intervals.
    # Bash randomness is sufficient here; these values are not security material.
    offset=$((RANDOM % 300))
    for ((i=0; i<100; ++i)); do
        candidates+=("$(((200 + (offset + i) % 300) * 100))")
    done
fi
for base in "${candidates[@]}"; do
    free=1
    # Check the entire range, including gaps between the named service ports.
    for ((port=base; port<=base+60; ++port)); do
        if [ "${occupied[$port]:-0}" = 1 ]; then
            free=0
            break
        fi
    done
    # This is a snapshot, not a reservation; the caller must still check identity.
    if [ "$free" = 1 ]; then
        echo "$base"
        exit 0
    fi
done
echo 'No free validator port block found' >&2
exit 1
