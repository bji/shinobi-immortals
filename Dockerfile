# Base image is Fedora 36
FROM fedora:36

# Do everything in /root instead of /
WORKDIR /root

# Download required files: bzip2, binutils (for strip command), Solana release, BPF tools, and Shinobi Immortals source
ADD https://ap.edge.kernel.org/fedora/releases/36/Everything/x86_64/os/Packages/b/bzip2-1.0.8-11.fc36.x86_64.rpm       \
    bzip2-1.0.8-11.fc36.x86_64.rpm
ADD https://ap.edge.kernel.org/fedora/releases/36/Everything/x86_64/os/Packages/b/binutils-2.37-24.fc36.x86_64.rpm     \
    binutils-2.37-24.fc36.x86_64.rpm
ADD https://github.com/solana-labs/solana/releases/download/v1.14.3/solana-release-x86_64-unknown-linux-gnu.tar.bz2    \
    solana-release-1.4.13-x86_64-unknown-linux-gnu.tar.bz2
ADD https://github.com/solana-labs/bpf-tools/releases/download/v1.29/solana-bpf-tools-linux.tar.bz2                    \
    solana-bpf-tools-v1.29-linux.tar.bz2
ADD https://github.com/bji/shinobi-immortals/tarball/master                                                            \
    shinobi-immortals.tar.gz

# Check SHA-1 hashes to ensure that the expected file contents were downloaded
RUN echo "13414d3c074d582253f6e38b336b87fd179b59ab bzip2-1.0.8-11.fc36.x86_64.rpm"                                     \
    | sha1sum -c -
RUN echo "e4dd9400ae2d7fbbc318fd1c1800d19032200c2b binutils-2.37-24.fc36.x86_64.rpm"                                   \
    | sha1sum -c -
RUN echo "8c8ace06cbd9f79ba053aaef6641b8073a4c0619 solana-release-1.4.13-x86_64-unknown-linux-gnu.tar.bz2"             \
    | sha1sum -c -
RUN echo "d89b3c0a369eabcdab663883431924f80f0a5d4c solana-bpf-tools-v1.29-linux.tar.bz2"                               \
    | sha1sum -c -

# Install bzip2
RUN rpm -i bzip2-1.0.8-11.fc36.x86_64.rpm

# Install binutils.  Dependencies are not needed for /bin/strip.
RUN rpm -i --nodeps binutils-2.37-24.fc36.x86_64.rpm

# Extract just the Solana SDK from the solana release
RUN tar jxf solana-release-1.4.13-x86_64-unknown-linux-gnu.tar.bz2 solana-release/bin/sdk

# Create the expected subdirectory within the Solana SDK for the BPF tools
RUN mkdir -p solana-release/bin/sdk/bpf/dependencies/bpf-tools

# Extract the BPF tools llvm component into the the Solana SDK
RUN (cd solana-release/bin/sdk/bpf/dependencies/bpf-tools; tar jxf /root/solana-bpf-tools-v1.29-linux.tar.bz2 ./llvm)

# Extract just build_program.sh and program
RUN tar zxf shinobi-immortals.tar.gz --strip-components=1 "*/build_program.sh" "*/program"

# Archive the Shinobi Immortals program directory so that it can be checked.  Use fixed timestamp so that the
# tar file is always the same.
RUN tar cf program.tar program --mtime "1970-01-01"

# Check that the SHA-1 hash of build_program.sh and program is as expected
RUN echo "316d12f3f58b2ee297ade86b24f90318c9fbf168 build_program.sh" | sha1sum -c -
RUN echo "0943c156ba2280622bc0c5e5568a8d99d8a21629 program.tar" | sha1sum -c -

# Run build_program.sh to build it
RUN SDK_ROOT=solana-release/bin/sdk sh build_program.sh

# Check to make sure that the sha1sum of the built program is as expected
RUN echo "8a366b6f0f03fbfa0647a992114a127aae2731e5 program.so" | sha1sum -c -
