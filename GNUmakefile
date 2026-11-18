SDK_ROOT?=./sdk

program.so: $(wildcard program/*.c program/*.h program/*/*.c program/*/*.h) build_program.sh
	SDK_ROOT=$(SDK_ROOT) SOURCE_ROOT=. ./build_program.sh

build_program.sh: make_build_program.sh
	./make_build_program.sh program-key.json super-key.json BLADE1qNA1uNjRgER6DtUFf7FU3c1TWLLdpPeEcKatZ2 > $@

.PHONY: test base-tests test-calculations test-destake-deactivated-elsewhere test-master-stake-recovery \
        test-destake-commission-waiver
test: base-tests test-calculations test-destake-deactivated-elsewhere test-master-stake-recovery \
      test-destake-commission-waiver

base-tests:
	SOURCE=`pwd` ./test/test.sh

test-calculations:
	./test/test_calculations.sh

test-destake-deactivated-elsewhere:
	SOURCE=`pwd` ./test/test_destake_deactivated_elsewhere.sh

test-master-stake-recovery:
	SOURCE=`pwd` ./test/test_master_stake_recovery.sh

test-destake-commission-waiver:
	SOURCE=`pwd` ./test/test_destake_commission_waiver.sh
