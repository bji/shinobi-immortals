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
#include "util/util_stake.c"
#include "util/util_whitelist.c"

// Withdraws to the admin the excess lamports of an account that the program controls.
//
// Anyone can transfer lamports to any account, and no other instruction can take them out of the accounts that this
// program controls.  This instruction recovers them.  The "excess" of an account is whatever it holds beyond what it
// is required to hold, and what it is required to hold depends on the type of account.  The instruction data names the
// type, and the account must really be an account of that type:
//
// - Master stake account: required to hold its rent exempt reserve, plus the larger of
//   MASTER_STAKE_ACCOUNT_MIN_LAMPORTS and, if it is actively delegated, its delegated stake.  So the excess is its
//   lamports that are not staked, except that MASTER_STAKE_ACCOUNT_MIN_LAMPORTS is always left in the account whether
//   or not those lamports are currently staked.  Lamports that are not staked come about when the master stake
//   account is deactivated by the stake program's DeactivateDelinquent instruction (which anyone may use after
//   Shinobi Systems has not voted for five epochs), and when SplitMasterStake merges a pre-merge account into it
//   (which adds that account's rent exempt reserve).  Leaving the minimum ensures that after a deactivation, what the
//   master stake account needs is still there to be delegated again by DelegateMasterStake, no matter in which order
//   the two instructions are used.
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
    WithdrawExcessAccountType_MasterStake   = 0,
    WithdrawExcessAccountType_ProgramState  = 1,
    WithdrawExcessAccountType_Authority     = 2,
    WithdrawExcessAccountType_Block         = 3,
    WithdrawExcessAccountType_Entry         = 4,
    WithdrawExcessAccountType_Whitelist     = 5
} WithdrawExcessAccountType;

typedef struct
{
    // This is the instruction code for WithdrawExcess
    uint8_t instruction_code;

    // The type of the account to withdraw from; a WithdrawExcessAccountType value
    uint8_t account_type;

} WithdrawExcessData;


// Computes the number of lamports that the master stake account is required to hold.  Returns false if the account
// is not a usable master stake account.
static bool master_stake_required_lamports(const SolAccountInfo *master_stake_account, uint64_t *required)
{
    // It must be the one true master stake account
    if (!is_master_stake_account(master_stake_account->key)) {
        return false;
    }

    // Decode the master stake account
    Stake stake;
    if (!decode_stake_account(master_stake_account, &stake)) {
        return false;
    }

    // The master stake account can only be Initialized or Stake; anything else is not a usable master stake account
    if ((stake.state != StakeState_Initialized) && (stake.state != StakeState_Stake)) {
        return false;
    }

    // The rent exempt reserve is never withdrawn.  The stake account records the reserve that it was created with;
    // use the larger of that and the current rent exempt minimum for a stake account, in case the two ever differ.
    uint64_t reserve = stake.meta.rent_exempt_reserve;
    uint64_t rent_exempt_minimum = get_rent_exempt_minimum(STAKE_ACCOUNT_DATA_LEN);
    if (rent_exempt_minimum > reserve) {
        reserve = rent_exempt_minimum;
    }

    // In addition to the reserve, MASTER_STAKE_ACCOUNT_MIN_LAMPORTS is always left in the account
    uint64_t to_keep = MASTER_STAKE_ACCOUNT_MIN_LAMPORTS;

    // If the master stake account is actively delegated (in the Stake state and not deactivated), then its delegated
    // stake cannot be withdrawn either; the stake program would refuse.  The delegated stake counts towards the
    // minimum, so what is kept is the larger of the two and not their sum.  This also covers stake that is still
    // activating, which the stake program treats as fully staked for the purposes of withdrawal.
    //
    // If it is not actively delegated, then the delegated stake recorded in the account is either cooling down, in
    // which case the stake program will refuse the withdrawal, or is no longer staked at all, in which case
    // everything beyond the minimum may be withdrawn.
    if ((stake.state == StakeState_Stake) && (stake.stake.delegation.deactivation_epoch == UINT64_MAX) &&
        (stake.stake.delegation.stake > to_keep)) {
        to_keep = stake.stake.delegation.stake;
    }

    // Guard against overflow of the sum.  This could only happen with an absurd delegation value, but would otherwise
    // make the required amount wrap to a small value.
    *required = reserve + to_keep;

    return (*required >= to_keep);
}


static uint64_t admin_withdraw_excess(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts.  The authority account, clock
    // sysvar, stake program and stake history sysvar are used only when withdrawing from the master stake account,
    // which is done by the stake program, but they are required for every account type so that the instruction has
    // a single form.
    //
    // The account to withdraw from may be the same account as the program state account or the authority account;
    // an account that appears more than once in a transaction is writable everywhere if it is writable anywhere.
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,  state_account,                 ReadOnly,   NotSigner,  KnownAccount_ProgramState);
        DECLARE_ACCOUNT(1,  admin_account,                 ReadWrite,  Signer,     KnownAccount_NotKnown);
        DECLARE_ACCOUNT(2,  from_account,                  ReadWrite,  NotSigner,  KnownAccount_NotKnown);
        DECLARE_ACCOUNT(3,  authority_account,             ReadOnly,   NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(4,  clock_sysvar_account,          ReadOnly,   NotSigner,  KnownAccount_ClockSysvar);
        DECLARE_ACCOUNT(5,  stake_program_account,         ReadOnly,   NotSigner,  KnownAccount_StakeProgram);
        DECLARE_ACCOUNT(6,  stake_history_sysvar_account,  ReadOnly,   NotSigner,  KnownAccount_StakeHistorySysvar);
    }
    DECLARE_ACCOUNTS_NUMBER(7);

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
    // them other than the master stake account are owned by this program, which the validation checks.
    uint64_t required;

    switch (data->account_type) {
    case WithdrawExcessAccountType_MasterStake:
        if (!master_stake_required_lamports(from_account, &required)) {
            return Error_InvalidAccount_First + 2;
        }
        break;

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

    // Move the excess to the admin
    if (data->account_type == WithdrawExcessAccountType_MasterStake) {
        // The master stake account is owned by the stake program, so the stake program must do the withdrawal, signed
        // by the program authority, which is the withdraw authority of the master stake account.  If the master stake
        // account has been deactivated and stake that this would withdraw is still cooling down, the stake program
        // refuses, which aborts the transaction; it will succeed once the cooldown has completed.
        withdraw_stake_signed(from_account->key, admin_account->key, excess, params->ka, params->ka_num);
    }
    else {
        // All of the other types of account are owned by this program, which can therefore move their lamports
        // directly
        *(from_account->lamports) -= excess;
        *(admin_account->lamports) += excess;
    }

    return 0;
}
