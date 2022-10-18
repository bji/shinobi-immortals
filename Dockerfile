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

# Cat build_program.sh and contents of all files under program into a single file so that the checksum can be verified
RUN find build_program.sh program -type f | sort | xargs cat > build_contents.txt

# Check that the SHA-1 hash of build_program.sh and program files is as expected
RUN echo "de701ed72badfe32f9db2577d9dab814a8865526 build_contents.txt" | sha1sum -c -

# Run build_program.sh to build it
RUN SDK_ROOT=solana-release/bin/sdk sh build_program.sh

# Check to make sure that the sha1sum of the built program is as expected
RUN echo "9b9cd31dcaf87b9b28fd4c2edb189cb6bf135175 program.so" | sha1sum -c -
