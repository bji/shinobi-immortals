#pragma once

#include "solana_sdk.h"


// Number of lamports per SOL (1 billion)
#define LAMPORTS_PER_SOL (1000ul * 1000ul * 1000ul)

// Minimum number of lamports allowed to be retained staked within the master stake account.  This must be a value
// that is always 2x greater than the minimum stake size (plus one for good measure!).  Because this is hardcoded, it
// has to anticipate the maximum possible minimum stake account size.
#define MASTER_STAKE_ACCOUNT_MIN_LAMPORTS (((2 * 1) + 1) * LAMPORTS_PER_SOL)

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
    PDA_Account_Seed_Prefix_Config = 1,

    PDA_Account_Seed_Prefix_Authority = 2,

    PDA_Account_Seed_Prefix_Master_Stake = 3,

    PDA_Account_Seed_Prefix_Ki_Mint = 4,

    PDA_Account_Seed_Prefix_Mint = 5,

    PDA_Account_Seed_Prefix_Token = 6,

    PDA_Obsolete_0 = 7,

    PDA_Obsolete_1 = 8,

    PDA_Account_Seed_Prefix_Bid = 9,

    PDA_Account_Seed_Prefix_Bridge = 10,

    PDA_Account_Seed_Prefix_Bid_Marker_Mint = 11,

    PDA_Account_Seed_Prefix_Bid_Marker_Token = 12,

    PDA_Account_Seed_Prefix_Whitelist = 13,

    PDA_Account_Seed_Prefix_Block = 14,

    PDA_Account_Seed_Prefix_Entry = 15,

    PDA_Account_Seed_Prefix_Master_Split = 16

} PDA_Account_Seed_Prefix;


// These are constant values that the program can use.  An instance of Constants is initialized by entrypoint and
// passed to all instruction entrypoints.  It would be ideal to have these values stored in a data segment but
// BPF is unable to do this.
typedef struct
{
    // This constant ensures that data conforming to the "Solana Security.txt" format is present in the binary.
    // See: https://github.com/neodyme-labs/solana-security-txt
    const char security_txt[304];

    // This is the pubkey bytes of the superuser
    SolPubkey superuser_pubkey;

    // This is the account address of the program config.  It is a program derived address using the
    // program id and config_seed_bytes
    SolPubkey config_pubkey;

    // These are the seed bytes used to derive the config_pubkey address
    uint8_t config_seed_bytes[2];

    // This is the account address of the authority account.  The authority account is used by the program as the
    // authority anywhere that an authority is needed, because the program can sign this authority.  This is computed
    // when the program's address is known and hardcoded into the program, along with the seeds needed to generate it.
    SolPubkey authority_pubkey;

    // These are the seed bytes used to derive the authority_pubkey address
    uint8_t authority_seed_bytes[2];

    // The master stake account to be used for commission splits.  This is needed to ensure that splits off of small
    // amounts of commission is possible, because if there is ever a minimum stake account size, then it would not be
    // possible to split less than that off of a stake account for commission purposes.  So instead, commission is
    // charged by: merging the stake account into the master stake account, then splitting off that same amount minus
    // the commission charge back into the original stake account.  The master stake account must have all authorities
    // set to the authority account, which will allow the program itself to have full control over the stake account.
    // Commission is taken from the master stake account to the admin account by a separate operation that the admin
    // can run which requests the program to split off SOL from the master stake account.  ALL authorities for the
    // stake account must be set to the authority account so that there are no trust issues.
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
    "policy\0https://www.shinobi-systems.com/immortals/security_policy.txt\0"
    "source_code\0http://github.com/shinobi_immortals\0"
    "=======END SECURITY.TXT V1=======\0",

    // superuser_pubkey
    SUPERUSER_PUBKEY_ARRAY,

    // config_pubkey
    CONFIG_PUBKEY_ARRAY,

    // config_seed_bytes
    { PDA_Account_Seed_Prefix_Config, CONFIG_BUMP_SEED },

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
