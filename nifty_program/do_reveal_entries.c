

// XXX cannot reveal entry if the SHA-256 hash in the entry is not the SHA-256 hash of this contiguous
// data: SHA-256 hash of NFT + SHA-256 hash of metaplex metadata account + SHA-256 hash of shinobi metadata account +
//       8 bytes of seed (seed supplied by transaction)

// XXX cannot reveal entry if the NFT is not owned by the nifty program

static uint64_t do_reveal_entries(SolParameters *params)
{
    return 0;
}
