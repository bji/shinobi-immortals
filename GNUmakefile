SDK_ROOT?=$(shell echo ~/.local/share/solana/install/active_release/bin/sdk)

program.so: $(wildcard program/*.c program/*.h program/*/*.c program/*/*.h)
	SDK_ROOT=$(SDK_ROOT) ./build_program.sh
