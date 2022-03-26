// This is the nifty stakes program id.  It is the account address that actually stores this program.
// Temporary devnet "dnp5NSZPf3WR4BUJvLUXWkbvULFESJmEZ6h55MxsssH"
#define NIFTY_STAKES_PROGRAM_ID_BYTES                                   \
    { 0x09, 0x6c, 0xb5, 0x61, 0x66, 0x75, 0x69, 0xa2,                   \
      0xb3, 0x92, 0xea, 0xd3, 0x29, 0x85, 0x9a, 0x60,                   \
      0x06, 0x25, 0x46, 0x8c, 0xfe, 0x4f, 0x3d, 0x0a,                   \
      0x20, 0x85, 0xfe, 0x39, 0xff, 0xcc, 0x4c, 0x2c }

// This is the System program id ("11111111111111111111111111111111" in base-58 which is all zeroes).
#define SYSTEM_PROGRAM_ID_BYTES                                         \
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,                   \
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,                   \
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,                   \
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }

// This is the address of the system account that contains the "program configuration" which includes an instance
// of ProgramConfig.
// Temporary devnet "Q4iZQMJvv7EtGrubJVnJGc9VGZShY8HRXCpitAeBViL"
#define PROGRAM_CONFIG_ACCOUNT_ADDRESS_BYTES                            \
    { 0x05, 0xe8, 0x8f, 0x97, 0x37, 0x8d, 0x5c, 0xab,                   \
      0x54, 0x94, 0x10, 0x2c, 0x97, 0x3c, 0x49, 0xca,                   \
      0xc4, 0xa9, 0x78, 0x5f, 0xb9, 0x25, 0x45, 0xc6,                   \
      0x2a, 0x65, 0xd2, 0x5c, 0xd3, 0xd6, 0xf7, 0x0d }

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
