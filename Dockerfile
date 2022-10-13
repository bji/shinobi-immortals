# Base image is Fedora 36
FROM fedora:36

# Do everything in /root instead of /
WORKDIR /root

# Download the solana release.
#ADD https://github.com/solana-labs/solana/releases/download/v1.14.3/solana-release-x86_64-unknown-linux-gnu.tar.bz2 solana-release-1.4.13-x86_64-unknown-linux-gnu.tar.bz2
COPY solana-release-1.4.13-x86_64-unknown-linux-gnu.tar.bz2 .

# Check that the SHA-1 hash of the solana release contents is as expected
RUN echo "8c8ace06cbd9f79ba053aaef6641b8073a4c0619 solana-release-1.4.13-x86_64-unknown-linux-gnu.tar.bz2" | sha1sum -c -

# Download the BPF tools
#ADD https://github.com/solana-labs/bpf-tools/releases/download/v1.29/solana-bpf-tools-linux.tar.bz2 solana-bpf-tools-v1.29-linux.tar.bz2
COPY solana-bpf-tools-v1.29-linux.tar.bz2 .

# Check that the SHA-1 hash of the BPF tools is as expected
RUN echo "d89b3c0a369eabcdab663883431924f80f0a5d4c solana-bpf-tools-v1.29-linux.tar.bz2" | sha1sum -c -

# Download binutils package for strip command
# ADD https://ap.edge.kernel.org/fedora/releases/36/Everything/x86_64/os/Packages/b/binutils-2.37-24.fc36.x86_64.rpm binutils-2.37-24.fc36.x86_64.rpm 
COPY binutils-2.37-24.fc36.x86_64.rpm .

# Check that the SHA-1 hash of the binutils is as expected
RUN echo "e4dd9400ae2d7fbbc318fd1c1800d19032200c2b binutils-2.37-24.fc36.x86_64.rpm" | sha1sum -c -

# Extract just the Solana SDK from the solana release
#RUN tar jxvf solana-release-1.4.13-x86_64-unknown-linux-gnu.tar.bz2 solana-release/bin/sdk
COPY solana-release ./solana-release

# Extract the BPF tools llvm component
#RUN tar jxvf solana-bpf-tools-v1.29-linux.tar.bz2 ./llvm
COPY llvm ./llvm

# Install binutils.  Dependencies are not needed for /bin/strip.
RUN rpm -i --nodeps binutils-2.37-24.fc36.x86_64.rpm

# Download nifty_stakes from github
# ADD XXX nifty_stakes.tar.gz
COPY nifty_stakes.tar.gz ./nifty_stakes.tar.gz

# Check that the SHA-1 hash of nifty_stakes is as expected
RUN echo "da93e5aacefbf8562811fec1a7518888003d01c5 nifty_stakes.tar.gz" | sha1sum -c -

# Extract just nifty_program from within nifty_stakes
RUN tar zxvf nifty_stakes.tar.gz nifty_stakes/fixed_bpf.ld nifty_stakes/nifty_program

# Set environment variables that define the pubkey derived values used to compile the program
ARG NIFTY_CONFIG_BUMP_SEED=255
ARG NIFTY_AUTHORITY_BUMP_SEED=255
ARG MASTER_STAKE_BUMP_SEED=254
ARG KI_MINT_BUMP_SEED=255
ARG BID_MARKER_MINT_BUMP_SEED=255
ARG SUPERUSER_PUBKEY_C_ARRAY="{149,75,37,119,54,246,197,190,16,143,112,158,97,119,81,215,238,210,166,209,105,8,8,93,247,219,12,212,49,92,124,122}"
ARG NIFTY_PROGRAM_PUBKEY_C_ARRAY="{12,253,21,64,142,230,43,86,84,142,172,211,194,8,126,44,9,38,242,227,243,83,139,34,35,97,21,160,177,193,77,241}"
ARG SYSTEM_PROGRAM_PUBKEY_C_ARRAY="{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}"
ARG METAPLEX_PROGRAM_PUBKEY_C_ARRAY="{11,112,101,177,227,209,124,69,56,157,82,127,107,4,195,205,88,184,108,115,26,160,253,181,73,182,209,188,3,248,41,70}"
ARG SPL_TOKEN_PROGRAM_PUBKEY_C_ARRAY="{6,221,246,225,215,101,161,147,217,203,225,70,206,235,121,172,28,180,133,237,95,91,55,145,58,140,245,133,126,255,0,169}"
ARG SPLATA_PROGRAM_PUBKEY_C_ARRAY="{140,151,37,143,78,36,137,241,187,61,16,41,20,142,13,131,11,90,19,153,218,255,16,132,4,142,123,216,219,233,248,89}"
ARG STAKE_PROGRAM_PUBKEY_C_ARRAY="{6,161,216,23,145,55,84,42,152,52,55,189,254,42,122,178,85,127,83,92,138,120,114,43,104,164,157,192,0,0,0,0}"
ARG CLOCK_SYSVAR_PUBKEY_C_ARRAY="{6,167,213,23,24,199,116,201,40,86,99,152,105,29,94,182,139,94,184,163,155,75,109,92,115,85,91,33,0,0,0,0}"
ARG RENT_SYSVAR_PUBKEY_C_ARRAY="{6,167,213,23,25,44,92,81,33,140,201,76,61,74,241,127,88,218,238,8,155,161,253,68,227,219,217,138,0,0,0,0}"
ARG STAKE_HISTORY_SYSVAR_PUBKEY_C_ARRAY="{6,167,213,23,25,53,132,208,254,237,155,179,67,29,19,32,107,229,68,40,27,87,184,86,108,197,55,95,244,0,0,0}"
ARG STAKE_CONFIG_SYSVAR_PUBKEY_C_ARRAY="{6,161,216,23,165,2,5,11,104,7,145,230,206,109,184,142,30,91,113,80,246,31,198,121,10,78,180,209,0,0,0,0}"
ARG SHINOBI_SYSTEMS_VOTE_PUBKEY_C_ARRAY="{35,64,91,198,151,178,39,110,134,135,254,240,218,101,223,64,25,43,105,167,108,89,153,148,23,100,82,150,108,61,5,190}"
ARG NIFTY_CONFIG_PUBKEY_C_ARRAY="{93,17,100,242,78,74,82,104,246,134,105,203,244,142,208,94,230,154,130,126,218,22,51,140,37,4,6,153,94,179,83,254}"
ARG NIFTY_AUTHORITY_PUBKEY_C_ARRAY="{199,237,182,189,144,255,200,79,201,210,57,42,68,97,122,32,47,205,236,233,124,2,95,23,154,84,39,154,3,253,98,243}"
ARG MASTER_STAKE_PUBKEY_C_ARRAY="{96,240,89,183,193,213,185,136,215,150,182,91,28,154,249,244,135,174,137,139,149,168,39,128,78,186,195,153,165,234,213,43}"
ARG KI_MINT_PUBKEY_C_ARRAY="{39,233,168,126,159,154,64,83,150,8,147,66,157,254,55,47,126,218,132,236,194,168,192,174,246,63,89,65,49,128,137,46}"
ARG KI_METADATA_PUBKEY_C_ARRAY="{7,153,132,208,149,212,112,196,8,105,65,138,172,59,108,243,199,197,96,177,218,185,178,54,44,85,105,136,131,112,30,97}"
ARG BID_MARKER_MINT_PUBKEY_C_ARRAY="{206,147,81,122,217,105,206,18,190,250,241,45,59,164,115,49,180,208,210,112,209,71,192,224,116,217,60,83,78,69,101,57}"
ARG BID_MARKER_METADATA_PUBKEY_C_ARRAY="{173,42,13,202,77,53,51,79,77,232,154,131,175,18,141,197,47,100,155,127,210,181,14,65,190,9,39,39,3,29,99,97}"

# Compile the program object file
RUN llvm/bin/clang                                                                                                     \
    -fno-builtin                                                                                                       \
    -fno-zero-initialized-in-bss                                                                                       \
    -fno-data-sections -std=c2x                                                                                        \
    -Isolana-release/bin/sdk/bpf/c/inc                                                                                 \
    -O3                                                                                                                \
    -target bpf                                                                                                        \
    -fPIC                                                                                                              \
    -march=bpfel+solana                                                                                                \
    -I nifty_stakes/nifty_program                                                                                      \
    -o nifty_program.po                                                                                                \
    -c nifty_stakes/nifty_program/entrypoint.c                                                                         \
    -DSUPERUSER_PUBKEY_ARRAY=${SUPERUSER_PUBKEY_C_ARRAY}                                                               \
    -DNIFTY_CONFIG_PUBKEY_ARRAY=${NIFTY_CONFIG_PUBKEY_C_ARRAY}                                                         \
    -DNIFTY_CONFIG_BUMP_SEED=${NIFTY_CONFIG_BUMP_SEED}                                                                 \
    -DNIFTY_AUTHORITY_PUBKEY_ARRAY=${NIFTY_AUTHORITY_PUBKEY_C_ARRAY}                                                   \
    -DNIFTY_AUTHORITY_BUMP_SEED=${NIFTY_AUTHORITY_BUMP_SEED}                                                           \
    -DMASTER_STAKE_PUBKEY_ARRAY=${MASTER_STAKE_PUBKEY_C_ARRAY}                                                         \
    -DMASTER_STAKE_BUMP_SEED=${MASTER_STAKE_BUMP_SEED}                                                                 \
    -DKI_MINT_PUBKEY_ARRAY=${KI_MINT_PUBKEY_C_ARRAY}                                                                   \
    -DKI_MINT_BUMP_SEED=${KI_MINT_BUMP_SEED}                                                                           \
    -DKI_METADATA_PUBKEY_ARRAY=${KI_METADATA_PUBKEY_C_ARRAY}                                                           \
    -DBID_MARKER_MINT_PUBKEY_ARRAY=${BID_MARKER_MINT_PUBKEY_C_ARRAY}                                                   \
    -DBID_MARKER_MINT_BUMP_SEED=${BID_MARKER_MINT_BUMP_SEED}                                                           \
    -DBID_MARKER_METADATA_PUBKEY_ARRAY=${BID_MARKER_METADATA_PUBKEY_C_ARRAY}                                           \
    -DSHINOBI_SYSTEMS_VOTE_PUBKEY_ARRAY=${SHINOBI_SYSTEMS_VOTE_PUBKEY_C_ARRAY}                                         \
    -DNIFTY_PROGRAM_PUBKEY_ARRAY=${NIFTY_PROGRAM_PUBKEY_C_ARRAY}                                                       \
    -DSYSTEM_PROGRAM_PUBKEY_ARRAY=${SYSTEM_PROGRAM_PUBKEY_C_ARRAY}                                                     \
    -DMETAPLEX_PROGRAM_PUBKEY_ARRAY=${METAPLEX_PROGRAM_PUBKEY_C_ARRAY}                                                 \
    -DSPL_TOKEN_PROGRAM_PUBKEY_ARRAY=${SPL_TOKEN_PROGRAM_PUBKEY_C_ARRAY}                                               \
    -DSPL_ASSOCIATED_TOKEN_ACCOUNT_PROGRAM_PUBKEY_ARRAY=${SPLATA_PROGRAM_PUBKEY_C_ARRAY}                               \
    -DSTAKE_PROGRAM_PUBKEY_ARRAY=${STAKE_PROGRAM_PUBKEY_C_ARRAY}                                                       \
    -DCLOCK_SYSVAR_PUBKEY_ARRAY=${CLOCK_SYSVAR_PUBKEY_C_ARRAY}                                                         \
    -DRENT_SYSVAR_PUBKEY_ARRAY=${RENT_SYSVAR_PUBKEY_C_ARRAY}                                                           \
    -DSTAKE_HISTORY_SYSVAR_PUBKEY_ARRAY=${STAKE_HISTORY_SYSVAR_PUBKEY_C_ARRAY}                                         \
    -DSTAKE_CONFIG_SYSVAR_PUBKEY_ARRAY=${STAKE_CONFIG_SYSVAR_PUBKEY_C_ARRAY}

# Link the program
RUN llvm/bin/ld.lld                                                                                                    \
    -z notext                                                                                                          \
    -shared                                                                                                            \
    --Bdynamic                                                                                                         \
    nifty_stakes/fixed_bpf.ld                                                                                          \
    --entry entrypoint                                                                                                 \
    -o nifty_program.so                                                                                                \
    nifty_program.po

# Strip the program
RUN strip -s -R .comment --strip-unneeded nifty_program.so

# Emit the sha1sum of the linked program.  This can be compared with the on-chain program to ensure that the on-chain
# program was built exactly as above.
RUN sha1sum nifty_program.so
