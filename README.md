
# Shinobi Immortals Program

## Introduction

This repository implements the Shinobi Immortals on-chain program.  It implements all
functionality related to the creation, initial purchase, staking, Ki harvesting, and
levelling of Shinobi Immortals digital assets.


## Location

The Shinobi Immortals program is published at the following address:

`Shin1cdrR1pmemXZU3yDV3PnQ48SX9UmrtHF4GbKzWG`


## Verifying

The program is intended to be securely verifiable by any user of the program.  To do
so, the following command line utilities are needed:

a. The solana command line program, available from Solana Labs: [https://docs.solana.com/cli/install-solana-cli-tools](https://docs.solana.com/cli/install-solana-cli-tools).

b. Docker, available here: [https://www.docker.com/](https://www.docker.com).

c. git, available here: [https://git-scm.com/](https://git-scm.com).

The following steps may be used to verify the program:

1. Check to make sure that the Shinobi Immortals program is non-upgradeable.  This is
   important because if the program is upgradeable, then it can be changed at any time
   and verification using these steps is only valid until it is changed.  To check:

   ```$ solana program show Shin1cdrR1pmemXZU3yDV3PnQ48SX9UmrtHF4GbKzWG```

   The result must include the line:

    ```Authority: none```

    This line indicates that there is no one authorized to upgrade the program, making it
    non-upgradeable.

    You may also check the program on the solana explorer:

    [https://explorer.solana.com/address/Shin1cdrR1pmemXZU3yDV3PnQ48SX9UmrtHF4GbKzWG](https://explorer.solana.com/address/Shin1cdrR1pmemXZU3yDV3PnQ48SX9UmrtHF4GbKzWG)

    The following will indicate that the program is not upgradeable:

    ```Upgradeable         No```
   
2. Fetch the contents of the published on-chain Solana program:

   ```$ solana program dump Shin1cdrR1pmemXZU3yDV3PnQ48SX9UmrtHF4GbKzWG shinobi-immortals.so```

3. Compute the sha256sum of the Shinobi Immortals program:

   ```$ sha256sum shinobi-immortals.so```

4. The resulting SHA-256 checksum should be:

   `bf46e8c9804ce323472a7fddac06d352d83e4cb1e69117811971a3a65c49daa5`

   This is the signature of the contents of the Shinobi Immortals program.

5. Checkout the Shinobi Immortals source code:

   ```$ git clone https://github.com/bji/shinobi-immortals.git```

6. Inspect the Shinobi Immortals source code that was just checked out.  Ensure that it does
   what it promises to do.  This requires software development expertise; if you don't have
   such expertise, find someone who does and who can help you.

7. Reproduce the build:

   ```$ docker build -t shinobi-immortals shinobi-immortals```

   This build will succeed only if the Shinobi Immortals program built from the source is
   identical to the on-chain program.  To verify this:

   a. Inspect the shinobi-immortals/Dockerfile.  Note that it only uses standard tooling.  Note
      also that it checks out the same Shinobi Immortals code that you checked out in step (5)
      so you can be sure that the code you have inspected is the code that was built.

   b. Note also that the Dockerfile builds using the `build_program.sh` script.  Inspect this
      file to ensure that it builds the program as you would expect.  Note that it includes
      hardcoded pubkeys and seeds; you can see how these were produced by looking at the
      make_build_program.sh script.

   c. Note that the Dockerfile will fail to build if the command that compiles the Shinobi
      Immortals program results in a file with a SHA-256 sum different than the one that was
      shown to be the signature of the on-chain program in step (1).

8. You have now inspected all files that go into building the Shinobi Immortals program, have
   verified that only open-source and standard tooling was used to build it, have verified that
   the source code you have access to is what the program was built from, and have verified
   that the on-chain program was built exactly from that source.  And because the program
   is non-upgradeable, it can never change and thus you know that the program will always be
   exactly that derived from the source you have inspected.


## Testing

If you like, you can run the tests which verify the functionality of the Shinobi Immortals program.
To do so, you will need solxact, available on github: [https://github.com/bji/solxact](https://github.com/bji/solxact).

You should verify the fidelity of these programs - the source is open and available.

Then, run the following command:

```$ SOURCE=`pwd`/shinobi-immortals ./shinobi-immortals/test/test.sh```

This will take a while to run to completion - about half an hour.  This is because it runs many
tests and it is conservative in waiting for commands to complete successfully before proceeding
to the next command.

You can inspect all of the tests that were run by looking at the files in the `test` directory.


## Issuing Manual Transactions


The shinobi-immortals repository contains scripts that can generate all transactions that interact with the Shinobi
Immortals program.  These are present in the scripts directory, and are named ending with '_tx.sh'.  The `solxact`
utility is required to use these scripts.  The generated transactions can be piped to solxact commands to apply recent
blockhash, sign, and submit the transactions.  Examples of doing this are present throughout the test scripts.


## Inspecting On-Chain State


The show.sh program in the scripts directory can be used to dump into JSON format all
on-chain account data associated with the Shinobi Immortals program.


## Programmatic Interaction with Shinobi Immortals

The js directory includes a Javascript API for interaction with Shinobi Immortals.


## License

The Shinobi Immortals program and all contents of this repository are Copyright 2022
by Shinobi Systems LLC, and licensed under the GNU General Public License Version 3.
