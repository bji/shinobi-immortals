#pragma once

#include "solana_sdk.h"


// Number of lamports per SOL (1 billion)
#define LAMPORTS_PER_SOL (1000ull * 1000ull * 1000ull)


// Each different PDA type has its own unique prefix, to ensure that even if other seed values between different PDA
// account types would result in overlapping addresses, the addresses in fact never overlap
typedef enum 
{
    PDA_Account_Seed_Prefix_Config = 1,
    
    PDA_Account_Seed_Prefix_Authority = 2,
    
    PDA_Account_Seed_Prefix_Mint = 3,
    
    PDA_Account_Seed_Prefix_Token = 4,

    PDA_Account_Seed_Prefix_Block = 5,

    PDA_Account_Seed_Prefix_Entry = 6,

    PDA_Account_Seed_Prefix_Bid = 7
    
} PDA_Account_Seed_Prefix;


// These are constant values that the program can use.  An instance of Constants is initialized by entrypoint and
// passed to all instruction entrypoints.  It would be ideal to have these values stored in a data segment but
// BPF is unable to do this.
typedef struct
{
    // This is the pubkey bytes of the superuser
    SolPubkey superuser_pubkey;

    // This is the account address of the program config.  It is a program derived address using the
    // program id and nifty_config_seed_bytes
    SolPubkey nifty_config_account;

    // These are the seed bytes used to derive the nifty_config_account address
    uint8_t nifty_config_seed_bytes[2];

    // This is the account address of the authority account.  The authority account is used by the program as the
    // authority anywhere that an authority is needed, because the program can sign this authority.  This is computed
    // when the program's address is known and hardcoded into the program, along with the seeds needed to generate it.
    SolPubkey nifty_authority_account;

    // These are the seed bytes used to derive the nifty_authority_account address
    uint8_t nifty_authority_seed_bytes[2];

    // The master stake account to be used for commission splits.  This is needed to ensure that splits off of
    // small amounts of commission is possible, because if there is ever a minimum stake account size, then it
    // would not be possible to split less than that off of a stake account for commission purposes.  So instead,
    // commission is charged by: merging the stake account into the master stake account, then splitting off
    // that same amount minus the commission charge back into the original stake account.  The master stake account
    // must have all authorities set to the nifty authority account, which will allow the nifty program itself
    // to have full control over the stake account.  Commission is taken from the master stake account to the
    // admin account by a separate operation that the admin can run which requests the program to split off SOL
    // from the master stake account.  ALL authorities for the stake account must be set to the nifty authority
    // account so that there are no trust issues.
    SolPubkey master_stake_account;

    // This is the Shinobi Systems vote account address
    SolPubkey shinobi_systems_vote_account;

    // This is the nifty program id.  It is the account address that actually stores this program.
    SolPubkey nifty_program_id;

    // This is the system program id
    SolPubkey system_program_id;

    // This is the rent program id
    SolPubkey rent_program_id;

    // This is the metaplex program id
    SolPubkey metaplex_program_id;

    // This is the Solana Program Library Token program id
    SolPubkey spl_token_program_id;

    // This is the stake program id
    SolPubkey stake_program_id;

} _Constants;


const _Constants Constants =
{
    // superuser_pubkey
    // DEVNET test value: dspaQ8kM3myRapUFqw8zRgwHJMUB5YcKKh7CxA1MWF1
    { 0x09, 0x72, 0x5f, 0x2d, 0xcf, 0x16, 0x13, 0x1c, 0x3d, 0x71, 0x3d, 0xf1, 0xf3, 0x66, 0x09, 0xc4,
      0xa1, 0xc7, 0x39, 0xff, 0xfe, 0xeb, 0x8e, 0x04, 0x67, 0x5f, 0x44, 0x59, 0x37, 0xb7, 0x37, 0x40 },
    
    // nifty_config_account
    // DEVNET test value: EfmgwpRCoUCdiq9Pn1CTaB49L3PnTKTCMvP8hzoo1XWX
    { 0xcb, 0x16, 0x87, 0xcd, 0x02, 0x6a, 0xee, 0x6b, 0x05, 0xab, 0x1e, 0x64, 0x4c, 0x24, 0x81, 0xae,
      0x3a, 0x37, 0x1a, 0x76, 0x44, 0x63, 0xc0, 0xe1, 0x41, 0x67, 0x50, 0x9d, 0xdc, 0x54, 0xdb, 0xc8 },
    
    // nifty_config_seed_bytes
    { PDA_Account_Seed_Prefix_Config, 254 },
    
    // nifty_authority_account
    // DEVNET test value: 4H5odRYyikgqXJXbYby95vKsDMFJkuBPdEAfeE8kSVKJ
    { 0x30, 0xb1, 0xc9, 0x19, 0x8d, 0x99, 0xd1, 0xb5, 0xc5, 0xc4, 0xef, 0x98, 0x09, 0x3c, 0x41, 0xec,
      0x0f, 0xe8, 0x47, 0x54, 0x70, 0xc0, 0xe8, 0xca, 0xb1, 0x80, 0x46, 0x34, 0x43, 0x72, 0x44, 0xcd },
    
    // nifty_authority_seed_bytes
    { PDA_Account_Seed_Prefix_Authority, 254 },

    // master_stake_account
    // DEVNET test value: dmsMXE4pyL7yZ2bmpgamFTuuSWJaDHfoXCMMJGT29RF
    { 0x09, 0x6b, 0xa4, 0x48, 0x3e, 0x13, 0x4f, 0x64, 0x87, 0x9b, 0x21, 0x17, 0xf5, 0x0b, 0x21, 0x34,
      0x8b, 0x30, 0x0a, 0xf6, 0xc7, 0x89, 0xe3, 0x5f, 0x4d, 0x56, 0xc8, 0x44, 0x6d, 0x5e, 0x17, 0x06 },

    // shinobi_systems_vote_account
    // DEVNET test value: vgcDar2pryHvMgPkKaZfh8pQy4BJxv7SpwUG7zinWjG
    { 0x0d, 0xc0, 0x91, 0x1e, 0x8a, 0x79, 0x90, 0x1e, 0x57, 0xb9, 0xb1, 0xc9, 0x6c, 0xb3, 0xd4, 0xb6,
      0x8c, 0x4b, 0x93, 0xc1, 0xc0, 0xf2, 0xda, 0x1c, 0x03, 0xa7, 0xb2, 0xf1, 0x77, 0xbe, 0x85, 0xdf },
    
    // nifty_program_id
    // DEVNET test value: dnpziZwxG5f99pEt3RTizWfpeavLDAZQnFShsTUntFL
    { 0x09, 0x6c, 0xb9, 0xf8, 0x85, 0xa9, 0xdf, 0xea, 0xd1, 0xdf, 0x22, 0x8c, 0x3f, 0x07, 0x5f, 0x7d,
      0x7a, 0x48, 0x71, 0xaa, 0x0e, 0x73, 0x47, 0xad, 0xa0, 0x72, 0x5b, 0xf7, 0xe0, 0xdf, 0x5a, 0xe3 },
    
    // system_program_id
    // 11111111111111111111111111111111
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    
    // rent_program_id
    // SysvarRent111111111111111111111111111111111
    { 0x06, 0xa7, 0xd5, 0x17, 0x19, 0x2c, 0x5c, 0x51, 0x21, 0x8c, 0xc9, 0x4c, 0x3d, 0x4a, 0xf1, 0x7f,
      0x58, 0xda, 0xee, 0x08, 0x9b, 0xa1, 0xfd, 0x44, 0xe3, 0xdb, 0xd9, 0x8a, 0x00, 0x00, 0x00, 0x00 },
    
    // metaplex_program_id
    // metaqbxxUerdq28cj1RbAWkYQm3ybzjb6a8bt518x1s
    { 0x0b, 0x70, 0x65, 0xb1, 0xe3, 0xd1, 0x7c, 0x45, 0x38, 0x9d, 0x52, 0x7f, 0x6b, 0x04, 0xc3, 0xcd,
      0x58, 0xb8, 0x6c, 0x73, 0x1a, 0xa0, 0xfd, 0xb5, 0x49, 0xb6, 0xd1, 0xbc, 0x03, 0xf8, 0x29, 0x46 },
    
    // spl_token_program_id
    // TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA
    { 0x06, 0xdd, 0xf6, 0xe1, 0xd7, 0x65, 0xa1, 0x93, 0xd9, 0xcb, 0xe1, 0x46, 0xce, 0xeb, 0x79, 0xac,
      0x1c, 0xb4, 0x85, 0xed, 0x5f, 0x5b, 0x37, 0x91, 0x3a, 0x8c, 0xf5, 0x85, 0x7e, 0xff, 0x00, 0xa9 },

    // stake_program_id
    // Stake11111111111111111111111111111111111111
    { 0x06, 0xa1, 0xd8, 0x17, 0x91, 0x37, 0x54, 0x2a, 0x98, 0x34, 0x37, 0xbd, 0xfe, 0x2a, 0x7a, 0xb2,
      0x55, 0x7f, 0x53, 0x5c, 0x8a, 0x78, 0x72, 0x2b, 0x68, 0xa4, 0x9d, 0xc0, 0x00, 0x00, 0x00, 0x00 }
};


// solana_sdk misses "const" in many places, so de-const to avoid compiler warnings.  The Constants instance
// is in a read-only data section and instructions which modify it actually have no effect.
#define Constants (* ((_Constants *) &Constants))
