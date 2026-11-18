#pragma once

#include "solana_sdk.h"


// Number of lamports per SOL (1 billion)
#define LAMPORTS_PER_SOL (1000ul * 1000ul * 1000ul)

// Minimum number of lamports that the program always keeps in the master stake account in addition to its rent
// exempt reserve: the master stake account is created with this much delegated, SplitMasterStake never splits its
// delegation below this, and WithdrawExcess never withdraws its balance below this.
//
// The master stake account must always be an actively delegated stake account whose delegation is at least the
// stake program's minimum delegation, because:
//
// - Commission is charged by using the stake program's MoveStake instruction to move stake from a user's stake account
//   into the master stake account.  MoveStake accepts any amount into an actively delegated destination whose
//   delegation, after the move, is at least the minimum delegation.  But if the destination is not delegated, the
//   amount moved must itself be at least the minimum delegation, which commission charges often are not; those
//   charges, and any Destake that owes them, would fail.
// - SplitMasterStake must leave at least the minimum delegation behind, or the stake program refuses the split.
// - If the master stake account is ever deactivated (see DelegateMasterStake), at least the minimum delegation must
//   remain in it to be delegated again.
//
// The stake program's minimum delegation is currently 1 lamport, but the stake program has a feature, not yet
// activated, which raises it to 1 SOL.  Because this program is not upgradeable, and because nothing can add to the
// master stake account's delegation other than commission charges themselves, this value is hardcoded with headroom
// above that: twice the largest minimum delegation that is anticipated.
#define MASTER_STAKE_ACCOUNT_MIN_LAMPORTS (2 * LAMPORTS_PER_SOL)

// The number of seconds after the end of an entry's auction during which only the winning bidder may settle the
// winning bid (see SettleWinningBid).  After that, anyone may settle it on the bidder's behalf.  This gives the
// winner of an auction 30 days in which to claim their win themselves, before the admin or anyone else can complete
// the auction for them.  It may only be overridden at build time, which is done only to build the program that the
// tests use, so that the tests do not have to wait 30 days.
#ifndef SETTLE_WINNING_BID_DELAY_SECONDS
#define SETTLE_WINNING_BID_DELAY_SECONDS (30ul * 24ul * 60ul * 60ul)
#endif

// The number of seconds by which a bid near the end of an auction extends it.  A bid placed with fewer than this many
// seconds left in the auction moves the auction's end to this many seconds after the bid, so that every bid leaves
// other bidders time to respond, and nothing is gained by bidding at the last moment.  There is no limit to the number
// of extensions: an auction continues for as long as bidders keep outbidding each other, each time by at least
// MINIMUM_BID_INCREMENT_BPS (see util_calculations.c).  It may only be overridden at build time, which is done only to
// build the program that the tests use.
#ifndef AUCTION_EXTENSION_SECONDS
#define AUCTION_EXTENSION_SECONDS (10ul * 60ul)
#endif

// The number of epochs after an entry's last completed commission charge at which Destake stops charging commission.
// Destake must be able to return a user's stake account even if commission can no longer be charged: a failed
// MoveStake aborts the whole transaction, so Destake cannot try the charge and carry on if it fails.  Instead, if
// commission is owed but no commission charge has completed for the entry in this many epochs, Destake does not
// attempt the charge, and the commission accrued since the last charge is forgone.
//
// Commission charges are made by the permissionless TakeCommissionOrDelegate crank, which the admin runs regularly
// to collect commission, so in normal operation no entry that owes commission ever goes this long without one.  If
// charging becomes impossible (for example while the master stake account cools down and warms up again after a
// DeactivateDelinquent, about 2 to 3 epochs, or after some future change to the stake program), or the admin stops
// running the crank, then after this many epochs users can always destake on their own.  An entry that owes no
// commission (at 0% commission, or with no rewards since its last charge) never attempts a MoveStake, so this does
// not apply to it.
//
// It may only be overridden at build time, which is done only to build the program that the tests use.
#ifndef DESTAKE_COMMISSION_WAIVER_EPOCHS
#define DESTAKE_COMMISSION_WAIVER_EPOCHS 5ul
#endif

// This is the Ki token name
#define KI_TOKEN_NAME "Ki"

// This is the Ki token symbol
#define KI_TOKEN_SYMBOL "KI"

// This is the Ki token metadata uri (to be updated with a permanent uri when one is available_
#define KI_TOKEN_METADATA_URI "https://www.shinobi-systems.com/nifty_stakes/ki.json"

// This is the Shinobi Immortals auction token name.  These tokens are used to create "markers" within users token
// lists that point to bids.  These must use the Metaplex Fungible Token standard, which means having at least one
// decimal place.
#define BID_MARKER_TOKEN_NAME "Shinobi Auction Bid Marker"

// Shinobi Immortals auction token symbol
#define BID_MARKER_TOKEN_SYMBOL "SHIN-BID"

// Shinobi Immortals auction token metadata uri
#define BID_MARKER_TOKEN_METADATA_URI "https://www.shinobi-systems.com/nifty_stakes/bid_marker.json"


// Each different PDA type has its own unique prefix, to ensure that even if other seed values between different PDA
// account types would result in overlapping addresses, the addresses in fact never overlap
typedef enum
{
    PDA_Account_Seed_Prefix_State = 1,

    PDA_Account_Seed_Prefix_Authority = 2,

    PDA_Account_Seed_Prefix_Master_Stake = 3,

    PDA_Account_Seed_Prefix_Ki_Mint = 4,

    PDA_Account_Seed_Prefix_Mint = 5,

    PDA_Account_Seed_Prefix_Token = 6,

    PDA_Obsolete_0 = 7,

    PDA_Obsolete_1 = 8,

    PDA_Account_Seed_Prefix_Bid = 9,

    PDA_Obsolete_2 = 10,

    PDA_Account_Seed_Prefix_Bid_Marker_Mint = 11,

    PDA_Account_Seed_Prefix_Bid_Marker_Token = 12,

    PDA_Account_Seed_Prefix_Whitelist = 13,

    PDA_Account_Seed_Prefix_Block = 14,

    PDA_Account_Seed_Prefix_Entry = 15,

    PDA_Obsolete_3 = 16

} PDA_Account_Seed_Prefix;


// These are constant values that the program can use.  An instance of Constants is initialized by entrypoint and
// passed to all instruction entrypoints.  It would be ideal to have these values stored in a data segment but
// BPF is unable to do this.
typedef struct
{
    // This constant ensures that data conforming to the "Solana Security.txt" format is present in the binary.
    // See: https://github.com/neodyme-labs/solana-security-txt
    const char security_txt[309];

    // This is the pubkey bytes of the superuser
    SolPubkey superuser_pubkey;

    // This is the account address of the program state.  It is a program derived address using the
    // program id and state_seed_bytes
    SolPubkey state_pubkey;

    // These are the seed bytes used to derive the state_pubkey address
    uint8_t state_seed_bytes[2];

    // This is the account address of the authority account.  The authority account is used by the program as the
    // authority anywhere that an authority is needed, because the program can sign this authority.  This is computed
    // when the program's address is known and hardcoded into the program, along with the seeds needed to generate it.
    SolPubkey authority_pubkey;

    // These are the seed bytes used to derive the authority_pubkey address
    uint8_t authority_seed_bytes[2];

    // The master stake account, which is where commission accumulates.  Commission is charged by using the stake
    // program's MoveStake instruction to move the commission amount of stake from a user's stake account directly
    // into the master stake account.  This works for commission of any size, however small, as long as the master
    // stake account is actively delegated with at least the stake program's minimum delegation; see
    // MASTER_STAKE_ACCOUNT_MIN_LAMPORTS.  The admin takes accumulated commission out of the master stake account
    // with SplitMasterStake, which splits stake off into a stake account that the admin controls.  ALL authorities
    // of the master stake account are the authority account, so that only the program itself has control over it and
    // there are no trust issues.
    SolPubkey master_stake_pubkey;

    // These are the seed bytes used to derive the master stake account address
    uint8_t master_stake_seed_bytes[2];

    // This is the Ki mint pubkey
    SolPubkey ki_mint_pubkey;

    // These are the seed bytes used to derive the Ki mint account address
    uint8_t ki_mint_seed_bytes[2];

    // This is the Ki mint metadata pubkey
    SolPubkey ki_metadata_pubkey;

    // This is the Shinobi Bid mint pubkey
    SolPubkey bid_marker_mint_pubkey;

    // These are the seed bytes used to derive the Shinobi Bid mint account address
    uint8_t bid_marker_mint_seed_bytes[2];

    // This is the Shinobi Bid mint metadata pubkey
    SolPubkey bid_marker_metadata_pubkey;

    // This is the Shinobi Systems vote account pubkey
    SolPubkey shinobi_systems_vote_pubkey;

    // This is the Shinobi Immortals program pubkey.  It is the account address that actually stores this program.
    SolPubkey self_program_pubkey;

    // This is the system program pubkey
    SolPubkey system_program_pubkey;

    // This is the metaplex program pubkey
    SolPubkey metaplex_program_pubkey;

    // This is the Solana Program Library Token program pubkey
    SolPubkey spl_token_program_pubkey;

    // This is the SPL Associated Token Account program pubkey
    SolPubkey spl_associated_token_account_program_pubkey;

    // This is the stake program pubkey
    SolPubkey stake_program_pubkey;

    // This is the clock sysvar pubkey
    SolPubkey clock_sysvar_pubkey;

    // This is the rent sysvar pubkey
    SolPubkey rent_sysvar_pubkey;

    // This is the stake history sysvar pubkey
    SolPubkey stake_history_sysvar_pubkey;

    // This is the stake config pubkey
    SolPubkey stake_config_pubkey;

} _Constants;


static const _Constants Constants =
{
    // security_txt
    "=======BEGIN SECURITY.TXT V1=======\0"
    "name\0Shinobi Immortals\0"
    "project_url\0https://www.shinobi-systems.com/immortals\0"
    "contacts\0email:shinobisystems@yahoo.com\0"
    "policy\0https://github.com/bji/shinobi_immortals/security_policy.txt\0"
    "source_code\0https://github.com/bji/shinobi_immortals\0"
    "=======END SECURITY.TXT V1=======\0",

    // superuser_pubkey
    SUPERUSER_PUBKEY_ARRAY,

    // state_pubkey
    STATE_PUBKEY_ARRAY,

    // state_seed_bytes
    { PDA_Account_Seed_Prefix_State, STATE_BUMP_SEED },

    // authority_pubkey
    AUTHORITY_PUBKEY_ARRAY,

    // authority_seed_bytes
    { PDA_Account_Seed_Prefix_Authority, AUTHORITY_BUMP_SEED },

    // master_stake_pubkey
    MASTER_STAKE_PUBKEY_ARRAY,

    // master_stake_seed_bytes
    { PDA_Account_Seed_Prefix_Master_Stake, MASTER_STAKE_BUMP_SEED },

    // ki_mint_pubkey
    KI_MINT_PUBKEY_ARRAY,

    // ki_mint_seed_bytes
    { PDA_Account_Seed_Prefix_Ki_Mint, KI_MINT_BUMP_SEED },

    // ki_metadata_pubkey
    KI_METADATA_PUBKEY_ARRAY,

    // bid_marker_mint_pubkey
    BID_MARKER_MINT_PUBKEY_ARRAY,

    // bid_marker_mint_seed_bytes
    { PDA_Account_Seed_Prefix_Bid_Marker_Mint, BID_MARKER_MINT_BUMP_SEED },

    // bid_marker_metadata_pubkey
    BID_MARKER_METADATA_PUBKEY_ARRAY,

    // shinobi_systems_vote_pubkey
    SHINOBI_SYSTEMS_VOTE_PUBKEY_ARRAY,

    // Shinobi Immortals program_pubkey
    SELF_PROGRAM_PUBKEY_ARRAY,

    // system_program_pubkey
    SYSTEM_PROGRAM_PUBKEY_ARRAY,

    // metaplex_program_pubkey
    METAPLEX_PROGRAM_PUBKEY_ARRAY,

    // spl_token_program_pubkey
    SPL_TOKEN_PROGRAM_PUBKEY_ARRAY,

    // spl_associated_token_account_program_pubkey
    SPL_ASSOCIATED_TOKEN_ACCOUNT_PROGRAM_PUBKEY_ARRAY,

    // stake_program_pubkey
    STAKE_PROGRAM_PUBKEY_ARRAY,

    // clock_sysvar_pubkey
    CLOCK_SYSVAR_PUBKEY_ARRAY,

    // rent_sysvar_pubkey
    RENT_SYSVAR_PUBKEY_ARRAY,

    // stake_history_sysvar_id
    STAKE_HISTORY_SYSVAR_PUBKEY_ARRAY,

    // stake_config_account
    STAKE_CONFIG_SYSVAR_PUBKEY_ARRAY
};


// solana_sdk misses "const" in many places, so de-const to avoid compiler warnings.  The Constants instance
// is in a read-only data section and instructions which modify it actually have no effect.
#define Constants (* ((_Constants *) &Constants))
