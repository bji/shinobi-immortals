#pragma once

#include "data_type.h"


// This is the format of data stored in the authority account.
//
// The authority account is the program derived address that the program signs with: it is the mint authority, freeze
// authority, metadata update authority and stake authority of everything that the program controls.  It is also
// where the purchase price of a mystery is held in escrow from the time that the mystery is bought until its entry is
// either revealed (the price then goes to the admin) or refunded (the price then goes back to the buyer).
//
// The total held in escrow is recorded here so that the program can always tell escrow apart from any other lamports
// that the authority account holds, such as lamports that someone transferred to it.  Only lamports beyond the escrow
// and the rent exempt minimum may ever be withdrawn by the admin (see the WithdrawExcess instruction); the escrow
// itself belongs to the buyers of unrevealed mysteries until reveal.
typedef struct
{
    // This is an indicator that the data is an Authority
    DataType data_type;

    // The total number of lamports held in escrow: the sum of the purchase prices of all entries that were bought as
    // mysteries and have since been neither revealed nor refunded.
    uint64_t mystery_escrow_lamports;

} Authority;
