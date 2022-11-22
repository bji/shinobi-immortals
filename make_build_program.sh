#!/bin/sh

# Builds a build script given the Shinobi Immortals program pubkey, superuser pubkey, and vote account.
# Requires that the solxact program be in the path.
# The resulting script requires the following variables to be defined:
# SDK_ROOT -- path to the root of the Solana SDK to use for building the program
# SOURCE_ROOT -- path to the Shinobi Immortals source

SELF_PROGRAM_PUBKEY=$1
SUPERUSER_PUBKEY=$2
SHINOBI_SYSTEMS_VOTE_PUBKEY=$3

if [ -z "$1" -o -z "$2" -o -z "$3" -o -n "$4" ]; then
    echo "Usage: make_build_script.sh <PROGRAM_PUBKEY> <SUPERUSER_PUBKEY> <VOTE_ACCOUNT_PUBKEY>"
    exit 1
fi

# pubkeys that are pre-defined
SYSTEM_PROGRAM_PUBKEY=11111111111111111111111111111111
SPL_TOKEN_PROGRAM_PUBKEY=TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA
SPLATA_PROGRAM_PUBKEY=ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL
STAKE_PROGRAM_PUBKEY=Stake11111111111111111111111111111111111111
CLOCK_SYSVAR_PUBKEY=SysvarC1ock11111111111111111111111111111111
RENT_SYSVAR_PUBKEY=SysvarRent111111111111111111111111111111111
METAPLEX_PROGRAM_PUBKEY=metaqbxxUerdq28cj1RbAWkYQm3ybzjb6a8bt518x1s
STAKE_HISTORY_SYSVAR_PUBKEY=SysvarStakeHistory1111111111111111111111111
STAKE_CONFIG_SYSVAR_PUBKEY=StakeConfig11111111111111111111111111111111

# program derived addresses, emitted by the solxact program in form PUBKEY.BUMP_SEED
CONFIG_PDA=`solxact pda bytes $SELF_PROGRAM_PUBKEY [ u8 1 ]`
AUTHORITY_PDA=`solxact pda bytes $SELF_PROGRAM_PUBKEY [ u8 2 ]`
MASTER_STAKE_PDA=`solxact pda bytes $SELF_PROGRAM_PUBKEY [ u8 3 ]`
KI_MINT_PDA=`solxact pda bytes $SELF_PROGRAM_PUBKEY [ u8 4 ]`
BID_MARKER_MINT_PDA=`solxact pda bytes $SELF_PROGRAM_PUBKEY [ u8 11 ]`

# pubkeys that are at fixed addresses
SYSTEM_PROGRAM_PUBKEY_ARRAY=`solxact pubkey bytes $SYSTEM_PROGRAM_PUBKEY`
METAPLEX_PROGRAM_PUBKEY_ARRAY=`solxact pubkey bytes $METAPLEX_PROGRAM_PUBKEY`
SPL_TOKEN_PROGRAM_PUBKEY_ARRAY=`solxact pubkey bytes $SPL_TOKEN_PROGRAM_PUBKEY`
SPLATA_PROGRAM_PUBKEY_ARRAY=`solxact pubkey bytes $SPLATA_PROGRAM_PUBKEY`
STAKE_PROGRAM_PUBKEY_ARRAY=`solxact pubkey bytes $STAKE_PROGRAM_PUBKEY`
CLOCK_SYSVAR_PUBKEY_ARRAY=`solxact pubkey bytes $CLOCK_SYSVAR_PUBKEY`
RENT_SYSVAR_PUBKEY_ARRAY=`solxact pubkey bytes $RENT_SYSVAR_PUBKEY`
STAKE_HISTORY_SYSVAR_PUBKEY_ARRAY=`solxact pubkey bytes $STAKE_HISTORY_SYSVAR_PUBKEY`
STAKE_CONFIG_SYSVAR_PUBKEY_ARRAY=`solxact pubkey bytes $STAKE_CONFIG_SYSVAR_PUBKEY`
SHINOBI_SYSTEMS_VOTE_PUBKEY_ARRAY=`solxact pubkey bytes $SHINOBI_SYSTEMS_VOTE_PUBKEY`

# pubkeys derived as metaplex PDAs
KI_METADATA_PDA=`solxact pda bytes $METAPLEX_PROGRAM_PUBKEY                                                           \
                                   [ string metadata                                                                  \
                                     pubkey $METAPLEX_PROGRAM_PUBKEY                                                  \
                                     pubkey \`echo "$KI_MINT_PDA" | cut -d '.' -f 1\` ]`
BID_MARKER_METADATA_PDA=`solxact pda bytes $METAPLEX_PROGRAM_PUBKEY                                                   \
                                           [ string metadata                                                          \
                                             pubkey $METAPLEX_PROGRAM_PUBKEY                                          \
                                             pubkey \`echo "$BID_MARKER_MINT_PDA" | cut -d '.' -f 1\` ]`

# pubkeys that are stored in files
SUPERUSER_PUBKEY_ARRAY=`solxact pubkey bytes $SUPERUSER_PUBKEY`
SELF_PROGRAM_PUBKEY_ARRAY=`solxact pubkey bytes $SELF_PROGRAM_PUBKEY`

# pubkeys that are PDAs
CONFIG_PUBKEY_ARRAY=`echo $CONFIG_PDA | cut -d '.' -f 1`
AUTHORITY_PUBKEY_ARRAY=`echo $AUTHORITY_PDA | cut -d '.' -f 1`
MASTER_STAKE_PUBKEY_ARRAY=`echo $MASTER_STAKE_PDA | cut -d '.' -f 1`
KI_MINT_PUBKEY_ARRAY=`echo $KI_MINT_PDA | cut -d '.' -f 1`
KI_METADATA_PUBKEY_ARRAY=`echo $KI_METADATA_PDA | cut -d '.' -f 1`
BID_MARKER_MINT_PUBKEY_ARRAY=`echo $BID_MARKER_MINT_PDA | cut -d '.' -f 1`
BID_MARKER_METADATA_PUBKEY_ARRAY=`echo $BID_MARKER_METADATA_PDA | cut -d '.' -f 1`

# bump seeds that derive PDAs
CONFIG_BUMP_SEED=`echo $CONFIG_PDA | cut -d '.' -f 2`
AUTHORITY_BUMP_SEED=`echo $AUTHORITY_PDA | cut -d '.' -f 2`
MASTER_STAKE_BUMP_SEED=`echo $MASTER_STAKE_PDA | cut -d '.' -f 2`
KI_MINT_BUMP_SEED=`echo $KI_MINT_PDA | cut -d '.' -f 2`
BID_MARKER_MINT_BUMP_SEED=`echo $BID_MARKER_MINT_PDA | cut -d '.' -f 2`

# C versions of pubkey arrays
SUPERUSER_PUBKEY_C_ARRAY=`echo "$SUPERUSER_PUBKEY_ARRAY" | tr '[' '{' | tr ']' '}'`
SELF_PROGRAM_PUBKEY_C_ARRAY=`echo "$SELF_PROGRAM_PUBKEY_ARRAY" | tr '[' '{' | tr ']' '}'`
SYSTEM_PROGRAM_PUBKEY_C_ARRAY=`echo "$SYSTEM_PROGRAM_PUBKEY_ARRAY" | tr '[' '{' | tr ']' '}'`
METAPLEX_PROGRAM_PUBKEY_C_ARRAY=`echo "$METAPLEX_PROGRAM_PUBKEY_ARRAY" | tr '[' '{' | tr ']' '}'`
SPL_TOKEN_PROGRAM_PUBKEY_C_ARRAY=`echo "$SPL_TOKEN_PROGRAM_PUBKEY_ARRAY" | tr '[' '{' | tr ']' '}'`
SPLATA_PROGRAM_PUBKEY_C_ARRAY=`echo "$SPLATA_PROGRAM_PUBKEY_ARRAY" | tr '[' '{' | tr ']' '}'`
STAKE_PROGRAM_PUBKEY_C_ARRAY=`echo "$STAKE_PROGRAM_PUBKEY_ARRAY" | tr '[' '{' | tr ']' '}'`
CLOCK_SYSVAR_PUBKEY_C_ARRAY=`echo "$CLOCK_SYSVAR_PUBKEY_ARRAY" | tr '[' '{' | tr ']' '}'`
RENT_SYSVAR_PUBKEY_C_ARRAY=`echo "$RENT_SYSVAR_PUBKEY_ARRAY" | tr '[' '{' | tr ']' '}'`
STAKE_HISTORY_SYSVAR_PUBKEY_C_ARRAY=`echo "$STAKE_HISTORY_SYSVAR_PUBKEY_ARRAY" | tr '[' '{' | tr ']' '}'`
STAKE_CONFIG_SYSVAR_PUBKEY_C_ARRAY=`echo "$STAKE_CONFIG_SYSVAR_PUBKEY_ARRAY" | tr '[' '{' | tr ']' '}'`
SHINOBI_SYSTEMS_VOTE_PUBKEY_C_ARRAY=`echo "$SHINOBI_SYSTEMS_VOTE_PUBKEY_ARRAY" | tr '[' '{' | tr ']' '}'`
CONFIG_PUBKEY_C_ARRAY=`echo "$CONFIG_PUBKEY_ARRAY" | tr '[' '{' | tr ']' '}'`
AUTHORITY_PUBKEY_C_ARRAY=`echo "$AUTHORITY_PUBKEY_ARRAY" | tr '[' '{' | tr ']' '}'`
MASTER_STAKE_PUBKEY_C_ARRAY=`echo "$MASTER_STAKE_PUBKEY_ARRAY" | tr '[' '{' | tr ']' '}'`
KI_MINT_PUBKEY_C_ARRAY=`echo "$KI_MINT_PUBKEY_ARRAY" | tr '[' '{' | tr ']' '}'`
KI_METADATA_PUBKEY_C_ARRAY=`echo "$KI_METADATA_PUBKEY_ARRAY" | tr '[' '{' | tr ']' '}'`
BID_MARKER_MINT_PUBKEY_C_ARRAY=`echo "$BID_MARKER_MINT_PUBKEY_ARRAY" | tr '[' '{' | tr ']' '}'`
BID_MARKER_METADATA_PUBKEY_C_ARRAY=`echo "$BID_MARKER_METADATA_PUBKEY_ARRAY" | tr '[' '{' | tr ']' '}'`

cat <<EOF
#!/bin/bash

set -e

CONFIG_BUMP_SEED="$CONFIG_BUMP_SEED"
AUTHORITY_BUMP_SEED="$AUTHORITY_BUMP_SEED"
MASTER_STAKE_BUMP_SEED="$MASTER_STAKE_BUMP_SEED"
KI_MINT_BUMP_SEED="$KI_MINT_BUMP_SEED"
BID_MARKER_MINT_BUMP_SEED="$BID_MARKER_MINT_BUMP_SEED"
SUPERUSER_PUBKEY_C_ARRAY="$SUPERUSER_PUBKEY_C_ARRAY"
SELF_PROGRAM_PUBKEY_C_ARRAY="$SELF_PROGRAM_PUBKEY_C_ARRAY"
SYSTEM_PROGRAM_PUBKEY_C_ARRAY="$SYSTEM_PROGRAM_PUBKEY_C_ARRAY"
METAPLEX_PROGRAM_PUBKEY_C_ARRAY="$METAPLEX_PROGRAM_PUBKEY_C_ARRAY"
SPL_TOKEN_PROGRAM_PUBKEY_C_ARRAY="$SPL_TOKEN_PROGRAM_PUBKEY_C_ARRAY"
SPLATA_PROGRAM_PUBKEY_C_ARRAY="$SPLATA_PROGRAM_PUBKEY_C_ARRAY"
STAKE_PROGRAM_PUBKEY_C_ARRAY="$STAKE_PROGRAM_PUBKEY_C_ARRAY"
CLOCK_SYSVAR_PUBKEY_C_ARRAY="$CLOCK_SYSVAR_PUBKEY_C_ARRAY"
RENT_SYSVAR_PUBKEY_C_ARRAY="$RENT_SYSVAR_PUBKEY_C_ARRAY"
STAKE_HISTORY_SYSVAR_PUBKEY_C_ARRAY="$STAKE_HISTORY_SYSVAR_PUBKEY_C_ARRAY"
STAKE_CONFIG_SYSVAR_PUBKEY_C_ARRAY="$STAKE_CONFIG_SYSVAR_PUBKEY_C_ARRAY"
SHINOBI_SYSTEMS_VOTE_PUBKEY_C_ARRAY="$SHINOBI_SYSTEMS_VOTE_PUBKEY_C_ARRAY"
CONFIG_PUBKEY_C_ARRAY="$CONFIG_PUBKEY_C_ARRAY"
AUTHORITY_PUBKEY_C_ARRAY="$AUTHORITY_PUBKEY_C_ARRAY"
MASTER_STAKE_PUBKEY_C_ARRAY="$MASTER_STAKE_PUBKEY_C_ARRAY"
KI_MINT_PUBKEY_C_ARRAY="$KI_MINT_PUBKEY_C_ARRAY"
KI_METADATA_PUBKEY_C_ARRAY="$KI_METADATA_PUBKEY_C_ARRAY"
BID_MARKER_MINT_PUBKEY_C_ARRAY="$BID_MARKER_MINT_PUBKEY_C_ARRAY"
BID_MARKER_METADATA_PUBKEY_C_ARRAY="$BID_MARKER_METADATA_PUBKEY_C_ARRAY"

\$SDK_ROOT/bpf/dependencies/bpf-tools/llvm/bin/clang                                       \\
    -fno-builtin                                                                          \\
    -fno-zero-initialized-in-bss                                                          \\
    -fno-data-sections                                                                    \\
    -std=c2x                                                                              \\
    -I\$SDK_ROOT/bpf/c/inc                                                                 \\
    -O3                                                                                   \\
    -target bpf                                                                           \\
    -fPIC                                                                                 \\
    -march=bpfel+solana                                                                   \\
    -I\$SOURCE_ROOT/program                                                               \\
    -o program.po                                                                         \\
    -c \$SOURCE_ROOT/program/entrypoint.c                                                  \\
    -DSUPERUSER_PUBKEY_ARRAY="\$SUPERUSER_PUBKEY_C_ARRAY"                                  \\
    -DCONFIG_PUBKEY_ARRAY="\$CONFIG_PUBKEY_C_ARRAY"                                        \\
    -DCONFIG_BUMP_SEED="\$CONFIG_BUMP_SEED"                                                \\
    -DAUTHORITY_PUBKEY_ARRAY="\$AUTHORITY_PUBKEY_C_ARRAY"                                  \\
    -DAUTHORITY_BUMP_SEED="\$AUTHORITY_BUMP_SEED"                                          \\
    -DMASTER_STAKE_PUBKEY_ARRAY="\$MASTER_STAKE_PUBKEY_C_ARRAY"                            \\
    -DMASTER_STAKE_BUMP_SEED="\$MASTER_STAKE_BUMP_SEED"                                    \\
    -DKI_MINT_PUBKEY_ARRAY="\$KI_MINT_PUBKEY_C_ARRAY"                                      \\
    -DKI_MINT_BUMP_SEED="\$KI_MINT_BUMP_SEED"                                              \\
    -DKI_METADATA_PUBKEY_ARRAY="\$KI_METADATA_PUBKEY_C_ARRAY"                              \\
    -DBID_MARKER_MINT_PUBKEY_ARRAY="\$BID_MARKER_MINT_PUBKEY_C_ARRAY"                      \\
    -DBID_MARKER_MINT_BUMP_SEED="\$BID_MARKER_MINT_BUMP_SEED"                              \\
    -DBID_MARKER_METADATA_PUBKEY_ARRAY="\$BID_MARKER_METADATA_PUBKEY_C_ARRAY"              \\
    -DSHINOBI_SYSTEMS_VOTE_PUBKEY_ARRAY="\$SHINOBI_SYSTEMS_VOTE_PUBKEY_C_ARRAY"            \\
    -DSELF_PROGRAM_PUBKEY_ARRAY="\$SELF_PROGRAM_PUBKEY_C_ARRAY"                            \\
    -DSYSTEM_PROGRAM_PUBKEY_ARRAY="\$SYSTEM_PROGRAM_PUBKEY_C_ARRAY"                        \\
    -DMETAPLEX_PROGRAM_PUBKEY_ARRAY="\$METAPLEX_PROGRAM_PUBKEY_C_ARRAY"                    \\
    -DSPL_TOKEN_PROGRAM_PUBKEY_ARRAY="\$SPL_TOKEN_PROGRAM_PUBKEY_C_ARRAY"                  \\
    -DSPL_ASSOCIATED_TOKEN_ACCOUNT_PROGRAM_PUBKEY_ARRAY="\$SPLATA_PROGRAM_PUBKEY_C_ARRAY"  \\
    -DSTAKE_PROGRAM_PUBKEY_ARRAY="\$STAKE_PROGRAM_PUBKEY_C_ARRAY"                          \\
    -DCLOCK_SYSVAR_PUBKEY_ARRAY="\$CLOCK_SYSVAR_PUBKEY_C_ARRAY"                            \\
    -DRENT_SYSVAR_PUBKEY_ARRAY="\$RENT_SYSVAR_PUBKEY_C_ARRAY"                              \\
    -DSTAKE_HISTORY_SYSVAR_PUBKEY_ARRAY="\$STAKE_HISTORY_SYSVAR_PUBKEY_C_ARRAY"            \\
    -DSTAKE_CONFIG_SYSVAR_PUBKEY_ARRAY="\$STAKE_CONFIG_SYSVAR_PUBKEY_C_ARRAY"

\$SDK_ROOT/bpf/dependencies/bpf-tools/llvm/bin/ld.lld                                      \\
    -z notext                                                                             \\
    -shared                                                                               \\
    --Bdynamic                                                                            \\
    \$SOURCE_ROOT/program/fixed_bpf.ld                                                    \\
    --entry entrypoint                                                                    \\
    -o program.so                                                                         \\
    program.po

rm program.po

strip -s -R .comment --strip-unneeded program.so
EOF
