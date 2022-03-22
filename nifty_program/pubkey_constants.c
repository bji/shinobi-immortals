// This is the nifty stakes program id.  It is the account address that actually stores this program.
// Temporary testnet "Aicji51GmmhREFbpvJSbdqdggBqcHDQfyrrYNctiZhvH"
#define NIFTY_STAKES_PROGRAM_ID_BYTES                                   \
    { 0x90, 0x62, 0x67, 0x18, 0xf9, 0xfb, 0xb0, 0x21,                   \
      0x22, 0xb9, 0x6a, 0xcd, 0xd8, 0x06, 0x18, 0x5d,                   \
      0x1b, 0x3d, 0x1d, 0xdb, 0x62, 0xe5, 0xe5, 0x46,                   \
      0xaf, 0xc8, 0x7b, 0x69, 0x93, 0xad, 0xb9, 0x62 }

// This is the System program id ("11111111111111111111111111111111" in base-58 which is all zeroes).
#define SYSTEM_PROGRAM_ID_BYTES                                         \
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,                   \
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,                   \
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,                   \
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }

// This is the address of the system account that contains the "program configuration" which includes an instance
// of ProgramConfig.
// Temporary testnet "9GqWv8fd6XyJ41wLLtVih82EgGfF6CEEvWgMfS6fGbTC"
#define PROGRAM_CONFIG_ACCOUNT_ADDRESS_BYTES                            \
    { 0x7a, 0xec, 0x10, 0x85, 0x12, 0x0f, 0xa6, 0x70,                   \
      0x29, 0x97, 0x02, 0x55, 0xdc, 0xd9, 0x95, 0x3f,                   \
      0xb4, 0xc7, 0xde, 0x49, 0x9e, 0xe2, 0x00, 0x06,                   \
      0xc4, 0xc6, 0xba, 0x8d, 0xba, 0xab, 0x6a, 0xcf }

// This is the address of the Shinobi Systems vote account.
// "BLADE1qNA1uNjRgER6DtUFf7FU3c1TWLLdpPeEcKatZ2"
#define SHINOBI_VOTE_ACCOUNT_ADDRESS_BYTES \
    { 0x99, 0x7d, 0x51, 0xbc, 0x6d, 0xc7, 0xaf, 0x75,                   \
      0x3c, 0x8e, 0xa7, 0x3b, 0x5f, 0xa4, 0xd9, 0xd0,                   \
      0x7d, 0x35, 0xf1, 0xd4, 0x07, 0xef, 0xb7, 0x00,                   \
      0x33, 0x10, 0xb4, 0xb1, 0x2c, 0x08, 0x0e, 0x55 }


static bool is_nifty_stakes_program(SolPubkey *pubkey)
{
    SolPubkey spk = NIFTY_STAKES_PROGRAM_ID_BYTES;
    
    return !sol_memcmp(pubkey, &spk, sizeof(SolPubkey));
}


static bool is_system_program(SolPubkey *pubkey)
{
    SolPubkey key = SYSTEM_PROGRAM_ID_BYTES;
    
    return !sol_memcmp(pubkey, &key, sizeof(SolPubkey));
}


static bool is_program_config_account(SolPubkey *pubkey)
{
    SolPubkey key = PROGRAM_CONFIG_ACCOUNT_ADDRESS_BYTES;

    return !sol_memcmp(pubkey, &key, sizeof(SolPubkey));
}


static bool is_shinobi_vote_account_address(SolPubkey *pubkey)
{
    SolPubkey key = SHINOBI_VOTE_ACCOUNT_ADDRESS_BYTES;

    return !sol_memcmp(pubkey, &key, sizeof(SolPubkey));
}
