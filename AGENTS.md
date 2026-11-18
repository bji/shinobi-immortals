The Shinobi Immortals Project
-----------------------------

This code repository is part of the Shinobi Immortals Project.  Here is a brief description of the project:

Shinobi Immortals is a Solana digital asset ecosystem that consists of:

- A set of digital assets intended for several purposes:

  1. To exist as collectibles
  2. To be used to earn Ki tokens
  3. To be used for on-chain gaming

- An on-chain program that creates, sells, manages auctions for, and handles staking, Ki harvesting, and levelling up for Shinobi Immortals

- Ki tokens, which can be used for various purposes within the Shinobi Immortals ecosystem, including levelling up Immortals, as game entry fees, and as in-game resources

- A Discord and associated bot that provides "expert level" interaction with the Shinobi Immortals ecosystem

- A Shinobi Immortals website that is a portal to Shinobi Immortals

- On-chain games that utilize the Shinobi Immortals digital assets as game pieces

- A creative team that designs and implements the Shinobi Immortals program, and associated games and other features, as well as creating the Immortals themselves

- An associated Solana validator: the Shinobi Systems validator, which is the only validator that can be staked to in order to earn Ki tokens


This Repository
---------------

This code repository implements the Shinobi Immortals on-chain Solana program.  It also hosts scripts and tools that facilitate administrative use of that program.  And it hosts (or should host) schemas and client libraries that can be used by end users (or websites and tools used by end users) to interact with the program.

Wherever possible, only use POSIX compatible programs and program options in all scripting.  bash, curl, and jq are OK.

Comment liberally.  A textual explanation line before non-trivial lines of scripts, for example, is preferred.

Do not run the full unit tests ("make test") on every change.  If you want to generate a temporary test script to validate an implementation detail, feel free to do it, but do not run the full 10 to 15 minute run of the test suite (make test) on every change.  I will let you know when to run it.  You can ask to run it if you feel it's important at the moment though.


Downstream
----------

Downstream of this repository would be:

- A Discord bot that provides commands for allowing Discord users to interact with all aspects of the Shinobi Immortals program.  This is intended to be an expert level feature and to facilitate early testing.  This is currently implemented but hasn't been used in years so may have bitrotted.

- A website that implements an interface into examining existing Shinobi Immortals, buying them, possibly selling them, and viewing a user's collection (not implemented yet)

- A first game that is implemented on-chain and that demonstrates the use of Shinobi Immortals as game pieces.  Not implemented yet, but there has been some early design effort.  It's called the "Training Grounds".

- Tooling, including possibly an internal website, that allows easy administrative access to the Shinobi Immortals ecosystem, including creating blocks of Shinobi Immortals for sale (not implemented yet)

- Tooling and processes for actually creating blocks of Shinobi Immortals assets, including all art and other aspects (not implemented yet)


Design Details of the Shinobi Immortals Program
-----------------------------------------------

It's written in C, not Rust.  The goal is minimal external dependencies so that there is little to no chance of unexpected behaviors or unaudited code from upstream-included libraries.  Rust will not be used to implement this program, even though Rust is by far the most common language for implementing Solana on-chain programs.

User verifiability and control is paramount.  For this reason, the program will be published as non-upgradeable, and also will be published with a reproducible build (VM-based) that can be used by end users to verify that the on-chain program was produced exactly from the published source code.  The program must be non-upgradeable so that end users can be cetain that there will be no rug pulling via a changed program.  When a user buys a Shinobi Immortal, they should have complete confidence that what they have bought is safe from being rug pulled.

For this reason, external dependencies are kept to a minimum and should only include calls into immutable programs as much as possible.  This is a reason why SPL Token is used instead of SPL Token 2022, the latter being newer but mutable.  Similarly, the immutable version of Metaplex is used for associating metadata with Immortals instead of newer upgradeable programs.

Because the program is non-upgradeable, any issues that need to be fixed, or additional functionality to add to the ecosystem that requires on-chain program support, must occur via publishing a new version, while retaining the old.  This is why the program has a "reauthorize" instruction, to allow end users to "move" their immortals over to being managed by a new on-chain program.  All design decisions about how the program will operate should respect the concept of non-upgradeability of the program, and should avoid anything that would lock the user in to the current program.  While the reauthorize instruction does require administrative approval (via signing), the fact that this instruction was created at all should give end users confidence that the goal is to facilitate this operation, not block it.


Design Details of the Assets
----------------------------

The assets are essentially NFTs; they are created using the SPL Token program, each with a unique token mint that mint onlys one token, after which mint authority is permanently revoked.  They are never referred to as NFTs though because the goal is that they represent something unique and far exceeding normal NFT semantics.

Ownership is via normal SPL Token ownership and is managed by that program.  The program also manages stake accounts on behalf of end users, associating stake accounts with Immortals and allowing those stake accounts to only be staked to the Shinobi Systems validator.  A stake account that is staked to an Immortal belongs to whoever owns the Immortal, so it's important that an Immortal can never be sold, given away, or destroyed while a stake account is attached to it: otherwise a user might sell or give away their stake account without realizing it, or lose it forever by burning the Immortal.  For this reason the program is the freeze authority of every Immortal's token mint, and it freezes the owner's token account for as long as a stake account is staked to the Immortal.  A staked Immortal cannot be transferred or burned; it must be destaked first, which returns the stake account to its owner and unfreezes the token.  The program only ever freezes a token account while the Immortal is staked, and unfreezes it when the stake account is returned.  The one exception is a destake in which the user asks for the token to be left frozen (the no_thaw flag of Destake), which exists so that a user can always get their stake account back even if unfreezing were ever to fail; the token then stays frozen until the user unfreezes it with the Thaw instruction, which only works while the Immortal is not staked, and which must be done before the Immortal can be staked again.
