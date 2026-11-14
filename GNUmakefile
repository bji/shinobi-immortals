SDK_ROOT?=./sdk

program.so: $(wildcard program/*.c program/*.h program/*/*.c program/*/*.h) build_program.sh
	SDK_ROOT=$(SDK_ROOT) SOURCE_ROOT=. ./build_program.sh

build_program.sh: make_build_program.sh
	./make_build_program.sh program-key.json super-key.json BLADE1qNA1uNjRgER6DtUFf7FU3c1TWLLdpPeEcKatZ2 > $@

.PHONY: test test-calculations test-destake-deactivated-elsewhere
test:
	SOURCE=`pwd` ./test/test.sh

test-calculations:
	./test/test_calculations.sh

test-destake-deactivated-elsewhere:
	SOURCE=`pwd` ./test_destake_deactivated_elsewhere.sh
