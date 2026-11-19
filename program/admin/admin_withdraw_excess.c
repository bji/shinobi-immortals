#pragma once

#include "inc/authority.h"
#include "inc/block.h"
#include "inc/entry.h"
#include "inc/program_state.h"
#include "inc/whitelist.h"
#include "util/util_accounts.c"
#include "util/util_block.c"
#include "util/util_entry.c"
#include "util/util_rent.c"
#include "util/util_whitelist.c"

// Withdraws to the admin the excess lamports of an account that the program controls.
//
// Anyone can transfer lamports to any account, and no other instruction can take them out of the accounts that this
// program controls.  This instruction recovers them.  The "excess" of an account is whatever it holds beyond what it
// is required to hold, and what it is required to hold depends on the type of account.  The instruction data names the
// type, and the account must really be an account of that type:
//
// - Authority account: required to hold its rent exempt minimum, plus the purchase prices of all mysteries that have
//   been bought and not yet revealed or refunded.  That escrow belongs to the buyers until reveal, and is never
//   excess; the authority account records its total (see inc/authority.h).
//
// - Program state account, and block, entry and whitelist accounts: required to hold their rent exempt minimum.
//
// The following are deliberately not supported:
//
// - Bid accounts.  The lamports of a bid account are the bid, so lamports transferred to one cannot be told apart from
//   it.  They go back to the bidder: with the bid when a losing bid is claimed, and as the excess beyond the bid
//   when a winning bid is settled (see SettleWinningBid).
// - Stake accounts that users have staked to their entries.  These belong to their users, and the admin must never be
//   able to withdraw from them.
// - Mint and token accounts.  These are owned by the SPL Token program.
//
// The amount withdrawn is always the entire excess; the instruction takes no amount.

// The types of account that excess lamports may be withdrawn from
typedef enum
{
    WithdrawExcessAccountType_ProgramState  = 0,
    WithdrawExcessAccountType_Authority     = 1,
    WithdrawExcessAccountType_Block         = 2,
    WithdrawExcessAccountType_Entry         = 3,
    WithdrawExcessAccountType_Whitelist     = 4
} WithdrawExcessAccountType;

typedef struct
{
    // This is the instruction code for WithdrawExcess
    uint8_t instruction_code;

    // The type of the account to withdraw from; a WithdrawExcessAccountType value
    uint8_t account_type;

} WithdrawExcessData;


static uint64_t admin_withdraw_excess(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts.
    //
    // The account to withdraw from may be the same account as the program state account or the authority account;
    // an account that appears more than once in a transaction is writable everywhere if it is writable anywhere.
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,  state_account,                 ReadOnly,   NotSigner,  KnownAccount_ProgramState);
        DECLARE_ACCOUNT(1,  admin_account,                 ReadWrite,  Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,  from_account,                  ReadWrite,  NotSigner,  KnownAccount_NotKnown);
    }
    DECLARE_ACCOUNTS_NUMBER(3);

    // Ensure that this transaction is admin authenticated
    if (!is_admin_account(state_account, admin_account->key)) {
        return Error_PermissionDenied;
    }

    // Ensure that the instruction data is exactly the correct size, with no trailing data
    if (params->data_len != sizeof(WithdrawExcessData)) {
        return Error_InvalidDataSize;
    }

    const WithdrawExcessData *data = (WithdrawExcessData *) params->data;

    // Validate that the account to withdraw from really is an account of the type named, and compute the number of
    // lamports that an account of that type is required to hold.  Each type is validated in the same way that the
    // other instruction handlers validate it, so that only genuine accounts of the program are accepted.  All of
    // them are owned by this program, which the validation checks.
    uint64_t required;

    switch (data->account_type) {
    case WithdrawExcessAccountType_ProgramState:
        // It must be the one true program state account
        if (!is_state_account(from_account->key)) {
            return Error_InvalidAccount_First + 2;
        }
        required = get_rent_exempt_minimum(from_account->data_len);
        break;

    case WithdrawExcessAccountType_Authority: {
        // It must be the one true authority account, which always holds an Authority
        if (!is_authority_account(from_account->key)) {
            return Error_InvalidAccount_First + 2;
        }
        const Authority *authority = (Authority *) from_account->data;
        // The mystery purchase prices held in escrow are not excess.  Guard against overflow of the sum, which would
        // make the required amount wrap to a small value and expose the escrow.
        uint64_t rent_exempt_minimum = get_rent_exempt_minimum(from_account->data_len);
        required = rent_exempt_minimum + authority->mystery_escrow_lamports;
        if (required < rent_exempt_minimum) {
            return Error_NothingToWithdraw;
        }
        break;
    }

    case WithdrawExcessAccountType_Block:
        if (!get_validated_block(from_account)) {
            return Error_InvalidAccount_First + 2;
        }
        required = get_rent_exempt_minimum(from_account->data_len);
        break;

    case WithdrawExcessAccountType_Entry:
        if (!get_validated_entry(from_account)) {
            return Error_InvalidAccount_First + 2;
        }
        required = get_rent_exempt_minimum(from_account->data_len);
        break;

    case WithdrawExcessAccountType_Whitelist:
        if (!get_validated_whitelist(from_account)) {
            return Error_InvalidAccount_First + 2;
        }
        required = get_rent_exempt_minimum(from_account->data_len);
        break;

    default:
        // Not a type of account that excess lamports may be withdrawn from
        return Error_InvalidData_First + 1;
    }

    // If the account holds no more than it is required to hold, there is nothing to withdraw
    uint64_t lamports = *(from_account->lamports);
    if (lamports <= required) {
        return Error_NothingToWithdraw;
    }

    uint64_t excess = lamports - required;

    // Move the excess to the admin.  All of the types of account are owned by this program, which can therefore move
    // their lamports directly.
    *(from_account->lamports) -= excess;
    *(admin_account->lamports) += excess;

    return 0;
}
