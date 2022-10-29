SDK_ROOT?=$(shell echo ~/.local/share/solana/install/active_release/bin/sdk)

program.so: $(wildcard program/*.c program/*.h program/*/*.c program/*/*.h) build_program.sh
	SDK_ROOT=$(SDK_ROOT) SOURCE_ROOT=. ./build_program.sh

build_program.sh: make_build_program.sh
	./make_build_program.sh program-key.json super-key.json BLADE1qNA1uNjRgER6DtUFf7FU3c1TWLLdpPeEcKatZ2 > $@
