#pragma once


typedef struct
{
    // This is an indicator that the data is a Bid
    DataType data_type;

    // The mint of the token that was bid on
    SolPubkey mint_pubkey;

    // The bidder account
    SolPubkey bidder_pubkey;
    
} Bid;
