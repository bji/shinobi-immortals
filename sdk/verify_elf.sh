#!/bin/bash
# Check SBPFv3 ELF invariants using integer byte reads on the pinned LE host.
set -euo pipefail
# Make hexadecimal/string comparisons independent of the user's locale.
export LC_ALL=C
ELF=${1:?Usage: verify_elf.sh PROGRAM.so}
function fail ()
{
    echo "$*" >&2
    exit 1
}
# od reads native unsigned integers; the pinned host must therefore be little endian.
[ "$(uname -s)/$(uname -m)" = Linux/x86_64 ] || fail 'ELF verifier requires Linux x86_64'
# wc may pad its decimal output; strip whitespace before later integer comparisons.
FILE_SIZE=$(wc -c < "$ELF")
FILE_SIZE=${FILE_SIZE//[[:space:]]/}
# An ELF64 file header occupies 64 bytes, before any program headers or segments.
[ "$FILE_SIZE" -ge 64 ] || fail 'Truncated ELF header'
# e_ident: ELF magic, 64-bit class, little-endian data, version 1, System V ABI,
# ABI version 0 and seven padding bytes. Compare bytes rather than native integers.
IDENTITY=$(od -An -v -tx1 -N16 "$ELF" | tr -d ' \n')
[ "$IDENTITY" = 7f454c46020101000000000000000000 ] || fail 'Invalid SBPFv3 ELF identity'
# Read one unsigned field: argument 1 is its byte offset; argument 2 is its width.
function number ()
{
    local value
    # -j skips to the field, -N limits the bytes read, and -tu selects its integer width.
    value=$(od -An -v "-tu$2" -j "$1" -N "$2" "$ELF" | tr -d ' \n')
    [[ "$value" =~ ^[0-9]+$ ]] || fail 'Truncated ELF field'
    # Reject oversized fields before Bash can interpret them as signed values.
    if [ "${#value}" -gt 19 ] || { [ "${#value}" = 19 ] && [[ "$value" > 9223372036854775807 ]]; }; then
        fail 'ELF field exceeds the verifier integer range'
    fi
    echo "$value"
}
# Fixed ELF64 offsets and required values:
#   18: e_machine=247 (EM_BPF); 20: e_version=1; 48: e_flags=3 (SBPFv3);
#   52: e_ehsize=64; 32: e_phoff=64; 54: e_phentsize=56.
[ "$(number 18 2)" = 247 ] && [ "$(number 20 4)" = 1 ] &&
    [ "$(number 48 4)" = 3 ] && [ "$(number 52 2)" = 64 ] &&
    [ "$(number 32 8)" = 64 ] && [ "$(number 54 2)" = 56 ] || fail 'Not an SBPFv3 ELF'
# e_entry is the entrypoint virtual address; e_phnum counts 56-byte program headers.
ENTRY=$(number 24 8)
PHNUM=$(number 56 2)
# The first loadable segment must start immediately after the program-header table.
END=$((64 + PHNUM * 56))
[ "$END" -le "$FILE_SIZE" ] || fail 'Program headers exceed file'
LOADS=0
for ((i=0; i<PHNUM; ++i)); do
    # Each header starts after the 64-byte file header plus preceding 56-byte records.
    base=$((64 + i * 56))
    # p_type=1 means PT_LOAD. Non-loadable headers do not describe mapped program bytes.
    kind=$(number "$base" 4)
    if [ "$kind" != 1 ]; then
        continue
    fi
    # ELF64 program-header offsets: flags +4, file offset +8, virtual address +16,
    # physical address +24, file size +32, and memory size +40.
    permissions=$(number "$((base+4))" 4)
    offset=$(number "$((base+8))" 8)
    virtual=$(number "$((base+16))" 8)
    physical=$(number "$((base+24))" 8)
    size=$(number "$((base+32))" 8)
    memsize=$(number "$((base+40))" 8)
    # v3 segments are contiguous, eight-byte aligned, and have no zero-filled
    # tail: file size equals memory size and physical/virtual addresses agree.
    [ "$offset" = "$END" ] && [ "$((offset % 8))" = 0 ] && [ "$((size % 8))" = 0 ] &&
        [ "$size" = "$memsize" ] && [ "$physical" = "$virtual" ] || fail 'Invalid SBPFv3 segment layout'
    # Compare against the remaining bytes before adding, avoiding arithmetic overflow.
    [ "$size" -le "$((FILE_SIZE - END))" ] || fail 'SBPFv3 segment exceeds file'
    END=$((offset + size))
    # Required order: read-only data (PF_R=4) at 0, then executable code (PF_X=1)
    # at 2^32. Only the code segment may contain the entrypoint.
    case "$LOADS/$permissions/$virtual" in
        0/4/0) ;;
        1/1/4294967296)
            [ "$ENTRY" -ge "$virtual" ] && [ "$((ENTRY - virtual))" -lt "$size" ] ||
                fail 'Entrypoint is outside executable segment' ;;
        *) fail 'Unexpected SBPFv3 segments' ;;
    esac
    LOADS=$((LOADS+1))
done
# Both mappings must exist; accepting only one would miss a malformed v3 layout.
[ "$LOADS" = 2 ] || fail 'Missing SBPFv3 segments'
echo 'Verified ELF: SBPFv3 (flags 0x3), read-only data and executable segments'
