#!/bin/bash

set -e

CONFIG_BUMP_SEED="253"
AUTHORITY_BUMP_SEED="253"
MASTER_STAKE_BUMP_SEED="254"
KI_MINT_BUMP_SEED="254"
BID_MARKER_MINT_BUMP_SEED="255"
SUPERUSER_PUBKEY_C_ARRAY="{149,75,37,119,54,246,197,190,16,143,112,158,97,119,81,215,238,210,166,209,105,8,8,93,247,219,12,212,49,92,124,122}"
SELF_PROGRAM_PUBKEY_C_ARRAY="{6,149,144,19,8,245,102,183,158,26,193,55,60,148,13,81,199,75,57,89,95,27,215,64,186,226,144,201,123,225,194,181}"
SYSTEM_PROGRAM_PUBKEY_C_ARRAY="{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}"
METAPLEX_PROGRAM_PUBKEY_C_ARRAY="{11,112,101,177,227,209,124,69,56,157,82,127,107,4,195,205,88,184,108,115,26,160,253,181,73,182,209,188,3,248,41,70}"
SPL_TOKEN_PROGRAM_PUBKEY_C_ARRAY="{6,221,246,225,215,101,161,147,217,203,225,70,206,235,121,172,28,180,133,237,95,91,55,145,58,140,245,133,126,255,0,169}"
SPLATA_PROGRAM_PUBKEY_C_ARRAY="{140,151,37,143,78,36,137,241,187,61,16,41,20,142,13,131,11,90,19,153,218,255,16,132,4,142,123,216,219,233,248,89}"
STAKE_PROGRAM_PUBKEY_C_ARRAY="{6,161,216,23,145,55,84,42,152,52,55,189,254,42,122,178,85,127,83,92,138,120,114,43,104,164,157,192,0,0,0,0}"
CLOCK_SYSVAR_PUBKEY_C_ARRAY="{6,167,213,23,24,199,116,201,40,86,99,152,105,29,94,182,139,94,184,163,155,75,109,92,115,85,91,33,0,0,0,0}"
RENT_SYSVAR_PUBKEY_C_ARRAY="{6,167,213,23,25,44,92,81,33,140,201,76,61,74,241,127,88,218,238,8,155,161,253,68,227,219,217,138,0,0,0,0}"
STAKE_HISTORY_SYSVAR_PUBKEY_C_ARRAY="{6,167,213,23,25,53,132,208,254,237,155,179,67,29,19,32,107,229,68,40,27,87,184,86,108,197,55,95,244,0,0,0}"
STAKE_CONFIG_SYSVAR_PUBKEY_C_ARRAY="{6,161,216,23,165,2,5,11,104,7,145,230,206,109,184,142,30,91,113,80,246,31,198,121,10,78,180,209,0,0,0,0}"
SHINOBI_SYSTEMS_VOTE_PUBKEY_C_ARRAY="{153,125,81,188,109,199,175,117,60,142,167,59,95,164,217,208,125,53,241,212,7,239,183,0,51,16,180,177,44,8,14,85}"
CONFIG_PUBKEY_C_ARRAY="{26,21,252,100,73,255,25,248,94,227,120,120,249,113,53,36,113,237,170,43,187,183,62,194,166,34,66,237,109,236,110,25}"
AUTHORITY_PUBKEY_C_ARRAY="{186,193,43,88,220,236,24,145,18,203,161,235,22,247,146,24,211,203,155,16,206,217,14,109,99,137,59,47,41,102,244,35}"
MASTER_STAKE_PUBKEY_C_ARRAY="{96,184,234,112,62,93,161,109,202,99,255,123,42,28,108,237,238,214,22,123,226,179,190,167,61,218,198,211,26,222,220,171}"
KI_MINT_PUBKEY_C_ARRAY="{102,71,203,144,27,254,228,104,146,162,23,95,48,74,21,242,34,202,123,203,207,57,200,118,125,186,226,175,118,29,227,26}"
KI_METADATA_PUBKEY_C_ARRAY="{95,105,135,153,232,240,25,47,48,30,118,232,246,249,74,239,227,145,175,190,21,102,108,52,167,220,161,70,33,255,86,3}"
BID_MARKER_MINT_PUBKEY_C_ARRAY="{50,93,116,81,245,55,111,7,147,234,11,23,77,132,23,6,166,82,147,167,119,81,92,21,67,16,88,56,5,125,32,78}"
BID_MARKER_METADATA_PUBKEY_C_ARRAY="{255,14,199,239,218,36,110,120,153,227,116,80,206,96,6,69,203,81,57,48,172,48,204,169,246,51,99,9,4,232,31,2}"

$SDK_ROOT/bpf/dependencies/bpf-tools/llvm/bin/clang                                             \
    -fno-builtin                                                                                \
    -fno-zero-initialized-in-bss                                                                \
    -fno-data-sections                                                                          \
    -std=c2x                                                                                    \
    -I$SDK_ROOT/bpf/c/inc                                                                       \
    -O3                                                                                         \
    -target bpf                                                                                 \
    -fPIC                                                                                       \
    -march=bpfel+solana                                                                         \
    -Iprogram                                                                                   \
    -o program.po                                                                               \
    -c program/entrypoint.c                                                                     \
    -DSUPERUSER_PUBKEY_ARRAY="$SUPERUSER_PUBKEY_C_ARRAY"                                        \
    -DCONFIG_PUBKEY_ARRAY="$CONFIG_PUBKEY_C_ARRAY"                                              \
    -DCONFIG_BUMP_SEED="$CONFIG_BUMP_SEED"                                                      \
    -DAUTHORITY_PUBKEY_ARRAY="$AUTHORITY_PUBKEY_C_ARRAY"                                        \
    -DAUTHORITY_BUMP_SEED="$AUTHORITY_BUMP_SEED"                                                \
    -DMASTER_STAKE_PUBKEY_ARRAY="$MASTER_STAKE_PUBKEY_C_ARRAY"                                  \
    -DMASTER_STAKE_BUMP_SEED="$MASTER_STAKE_BUMP_SEED"                                          \
    -DKI_MINT_PUBKEY_ARRAY="$KI_MINT_PUBKEY_C_ARRAY"                                            \
    -DKI_MINT_BUMP_SEED="$KI_MINT_BUMP_SEED"                                                    \
    -DKI_METADATA_PUBKEY_ARRAY="$KI_METADATA_PUBKEY_C_ARRAY"                                    \
    -DBID_MARKER_MINT_PUBKEY_ARRAY="$BID_MARKER_MINT_PUBKEY_C_ARRAY"                            \
    -DBID_MARKER_MINT_BUMP_SEED="$BID_MARKER_MINT_BUMP_SEED"                                    \
    -DBID_MARKER_METADATA_PUBKEY_ARRAY="$BID_MARKER_METADATA_PUBKEY_C_ARRAY"                    \
    -DSHINOBI_SYSTEMS_VOTE_PUBKEY_ARRAY="$SHINOBI_SYSTEMS_VOTE_PUBKEY_C_ARRAY"                  \
    -DSELF_PROGRAM_PUBKEY_ARRAY="$SELF_PROGRAM_PUBKEY_C_ARRAY"                                  \
    -DSYSTEM_PROGRAM_PUBKEY_ARRAY="$SYSTEM_PROGRAM_PUBKEY_C_ARRAY"                              \
    -DMETAPLEX_PROGRAM_PUBKEY_ARRAY="$METAPLEX_PROGRAM_PUBKEY_C_ARRAY"                          \
    -DSPL_TOKEN_PROGRAM_PUBKEY_ARRAY="$SPL_TOKEN_PROGRAM_PUBKEY_C_ARRAY"                        \
    -DSPL_ASSOCIATED_TOKEN_ACCOUNT_PROGRAM_PUBKEY_ARRAY="$SPLATA_PROGRAM_PUBKEY_C_ARRAY"        \
    -DSTAKE_PROGRAM_PUBKEY_ARRAY="$STAKE_PROGRAM_PUBKEY_C_ARRAY"                                \
    -DCLOCK_SYSVAR_PUBKEY_ARRAY="$CLOCK_SYSVAR_PUBKEY_C_ARRAY"                                  \
    -DRENT_SYSVAR_PUBKEY_ARRAY="$RENT_SYSVAR_PUBKEY_C_ARRAY"                                    \
    -DSTAKE_HISTORY_SYSVAR_PUBKEY_ARRAY="$STAKE_HISTORY_SYSVAR_PUBKEY_C_ARRAY"                  \
    -DSTAKE_CONFIG_SYSVAR_PUBKEY_ARRAY="$STAKE_CONFIG_SYSVAR_PUBKEY_C_ARRAY"

$SDK_ROOT/bpf/dependencies/bpf-tools/llvm/bin/ld.lld                                            \
    -z notext                                                                                   \
    -shared                                                                                     \
    --Bdynamic                                                                                  \
    program/fixed_bpf.ld                                                                        \
    --entry entrypoint                                                                          \
    -o program.so                                                                               \
    program.po                                                                                  \

rm program.po

strip -s -R .comment --strip-unneeded program.so
