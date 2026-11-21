SDK_ROOT?=./build_tools/sdk

program.so: $(wildcard program/*.c program/*.h program/*/*.c program/*/*.h) build_program.sh
	SDK_ROOT=$(SDK_ROOT) SOURCE_ROOT=. ./build_program.sh

build_program.sh: make_build_program.sh
	./make_build_program.sh program-key.json super-key.json BLADE1qNA1uNjRgER6DtUFf7FU3c1TWLLdpPeEcKatZ2 > $@

.PHONY: test base-tests test-calculations test-destake-deactivated-elsewhere test-reference-stake-deactivated \
        test-auction-extension test-sdk-js test-sdk-rust sdk-js-packages clean clean-test clean-test-sdk-js \
        clean-test-sdk-rust clean-sdk-js-packages
test: base-tests test-calculations test-destake-deactivated-elsewhere test-reference-stake-deactivated \
      test-auction-extension test-sdk-js test-sdk-rust

base-tests:
	SOURCE=`pwd` ./test/program/test.sh

test-calculations:
	./test/program/test_calculations.sh

test-destake-deactivated-elsewhere:
	SOURCE=`pwd` ./test/program/test_destake_deactivated_elsewhere.sh

test-reference-stake-deactivated:
	SOURCE=`pwd` ./test/program/test_reference_stake_deactivated.sh

test-auction-extension:
	SOURCE=`pwd` ./test/program/test_auction_extension.sh

test-sdk-js:
	./test/sdk/js/run_tests.sh

test-sdk-rust:
	./test/sdk/rust/run_tests.sh

# Installs the npm packages that some of the Javascript SDK tests need (test/sdk/js/package.json), exactly as
# package-lock.json pins them.  --ignore-scripts keeps npm from running any install script of any package, which is a
# common way for a compromised package to run code; install them only this way, not with a bare "npm ci" or "npm
# install".
sdk-js-packages:
	cd test/sdk/js && npm ci --ignore-scripts

# Removes the built program.  build_program.sh is not removed, though make builds it too: it is checked in, and it can
# only be made again with the program's and superuser's key files (program-key.json and super-key.json), which are
# not.
clean:
	rm -f program.so

# Removes what the test targets leave behind.  The program tests (base-tests, test-calculations,
# test-destake-deactivated-elsewhere, test-reference-stake-deactivated, and test-auction-extension) build and run
# everything in temporary directories, which they remove themselves, so only the SDK tests leave anything here.
clean-test: clean-test-sdk-js clean-test-sdk-rust

# Removes the build output of the Javascript SDK tests' Rust reference programs (test/sdk/js/reference), which
# test-sdk-js builds and keeps so that later runs need not build them again
clean-test-sdk-js:
	rm -rf test/sdk/js/reference/target

# Removes the build output of the Rust SDK tests: cargo's output for the crate (sdk/rust), for the tests
# (test/sdk/rust), and for the example game programs, which test-sdk-rust keeps so that later runs need not build them
# again
clean-test-sdk-rust:
	rm -rf sdk/rust/target test/sdk/rust/target test/sdk/rust/example_game/target \
	       test/sdk/rust/example_game_pinocchio/target

# Removes the npm packages that sdk-js-packages installs.  Until they are installed again, the Javascript SDK tests
# that need them are skipped.
clean-sdk-js-packages:
	rm -rf test/sdk/js/node_modules
