
// An empty pubkey is all zeroes.  This is actually the program address of the system program, but when used
// as an account in entry data, it represents "no account"

static bool is_empty_pubkey(SolPubkey *pubkey)
{
    // This function is only called from contexts for which an "all zero pubkey" is considered to be "empty".
    for (uint8_t i = 0; i < sizeof(SolPubkey); i++) {
        if (pubkey->x[i]) {
            return false;
        }
    }

    return true;
}
