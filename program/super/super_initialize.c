#pragma once

#include "solana_sdk.h"

#include "inc/clock.h"
#include "inc/constants.h"
#include "inc/error.h"
#include "inc/instruction_accounts.h"
#include "inc/program_state.h"
#include "inc/types.h"
#include "util/util_accounts.c"
#include "util/util_calculations.c"
#include "util/util_metaplex.c"
#include "util/util_rent.c"
#include "util/util_stake.c"
#include "util/util_token.c"


// Data passed to this program for Initialize function
typedef struct
{
    uint8_t instruction_code;

    SolPubkey admin_pubkey;

    // The annual reward rate, in basis points, that LevelUp uses until MeasureRewardRate has measured one (see
    // reward_rate in inc/program_state.h): the superuser's estimate of the rate that a delegator to Shinobi Systems
    // earns.  It must be within REWARD_RATE_MIN_BPS and REWARD_RATE_MAX_BPS.  Measurements then move the rate from
    // it gradually (see compute_updated_reward_rate), by at most 1.25% per level credit window, so it should be a
    // good estimate.  It is set before any Immortal exists, so it gives the superuser no influence over how hard any
    // existing Immortal is to level.
    uint32_t initial_reward_bps;

} InitializeData;


static uint64_t super_initialize(const SolParameters *params)
{
    // Declare accounts, which checks the permissions and identity of all accounts
    DECLARE_ACCOUNTS {
        DECLARE_ACCOUNT(0,   superuser_account,             ReadWrite,  Signer,     KnownAccount_SuperUser);
        DECLARE_ACCOUNT(1,   state_account,                 ReadWrite,  NotSigner,  KnownAccount_ProgramState);
        DECLARE_ACCOUNT(2,   authority_account,             ReadWrite,  NotSigner,  KnownAccount_Authority);
        DECLARE_ACCOUNT(3,   ki_mint_account,               ReadWrite,  NotSigner,  KnownAccount_KiMint);
        DECLARE_ACCOUNT(4,   ki_metadata_account,           ReadWrite,  NotSigner,  KnownAccount_KiMetadata);
        DECLARE_ACCOUNT(5,   bid_marker_mint_account,       ReadWrite,  NotSigner,  KnownAccount_BidMarkerMint);
        DECLARE_ACCOUNT(6,   bid_marker_metadata_account,   ReadWrite,  NotSigner,  KnownAccount_BidMarkerMetadata);
        DECLARE_ACCOUNT(7,   rent_sysvar_account,           ReadOnly,   NotSigner,  KnownAccount_RentSysvar);
        DECLARE_ACCOUNT(8,   system_program_account,        ReadOnly,   NotSigner,  KnownAccount_SystemProgram);
        DECLARE_ACCOUNT(9,   spl_token_program_account,     ReadOnly,   NotSigner,  KnownAccount_SPLTokenProgram);
        DECLARE_ACCOUNT(10,  metaplex_program_account,      ReadOnly,   NotSigner,  KnownAccount_MetaplexProgram);
        // These are needed to create the reference stake account and delegate it to Shinobi Systems
        DECLARE_ACCOUNT(11,  reference_stake_account,       ReadWrite,  NotSigner,  KnownAccount_ReferenceStake);
        DECLARE_ACCOUNT(12,  shinobi_systems_vote_account,  ReadOnly,   NotSigner,  KnownAccount_ShinobiSystemsVote);
        DECLARE_ACCOUNT(13,  clock_sysvar_account,          ReadOnly,   NotSigner,  KnownAccount_ClockSysvar);
        DECLARE_ACCOUNT(14,  stake_history_sysvar_account,  ReadOnly,   NotSigner,  KnownAccount_StakeHistorySysvar);
        DECLARE_ACCOUNT(15,  stake_config_account,          ReadOnly,   NotSigner,  KnownAccount_StakeConfig);
        DECLARE_ACCOUNT(16,  stake_program_account,         ReadOnly,   NotSigner,  KnownAccount_StakeProgram);
    }
    DECLARE_ACCOUNTS_NUMBER(17);

    // Ensure that the input data is the correct size
    if (params->data_len != sizeof(InitializeData)) {
        return Error_InvalidDataSize;
    }

    const InitializeData *data = (InitializeData *) params->data;

    // The initial reward rate must be within the bounds that measurements are kept within
    if ((data->initial_reward_bps < REWARD_RATE_MIN_BPS) || (data->initial_reward_bps > REWARD_RATE_MAX_BPS)) {
        return Error_InvalidData_First + 2;
    }

    // Get the clock sysvar, needed to record the window of the initial reward rate.  The cluster's clock is never
    // before 1970, but don't allow a negative timestamp to be converted into a huge unsigned window number.
    Clock clock;
    if (sol_get_clock_sysvar(&clock) || (clock.unix_timestamp < 0)) {
        return Error_FailedToGetClock;
    }

    // If the program_state account already exists, then fail, because can't re-create the program_state account, can only modify it
    // after it's created
    if (state_account->data_len > 0) {
        return Error_InvalidAccount_First + 1;
    }

    // Create the program_state account.  The program_state account is derived from a fixed seed.
    {
        const uint8_t *seed_bytes = (uint8_t *) Constants.state_seed_bytes;
        SolSignerSeed seed = { seed_bytes, sizeof(Constants.state_seed_bytes) };

        uint64_t ret = create_pda(state_account, &seed, 1, superuser_account->key, &(Constants.self_program_pubkey),
                                  get_rent_exempt_minimum(sizeof(ProgramState)), sizeof(ProgramState), params->ka,
                                  params->ka_num);
        if (ret) {
            return ret;
        }

        // Set the initial contents of the program_state account.  The reward rate starts as the one given, as if it
        // had been measured in the current window, so that MeasureRewardRate first measures in a later window.  A
        // newly created account is all zeroes, so there is no reference stake account snapshot yet (see
        // MeasureRewardRate).  The rate in basis points is converted to a fraction of 2^32, rounded down; it is then
        // within REWARD_RATE_MIN and REWARD_RATE_MAX, and compute_reward_rate_bps gives back the same basis points.
        ProgramState *program_state = (ProgramState *) (state_account->data);
        program_state->data_type = DataType_ProgramState;
        program_state->admin_pubkey = data->admin_pubkey;
        program_state->reward_rate = (uint32_t) ((data->initial_reward_bps * REWARD_RATE_ONE) / 10000ul);
        program_state->reward_rate_window = ((uint64_t) clock.unix_timestamp) / LEVEL_CREDIT_WINDOW_SECONDS;
    }

    // Create the authority account.  The authority account is derived from a fixed seed.  Its data records the
    // total of the mystery purchase prices that it holds in escrow, which starts at zero.
    {
        const uint8_t *seed_bytes = (uint8_t *) Constants.authority_seed_bytes;
        SolSignerSeed seed = { seed_bytes, sizeof(Constants.authority_seed_bytes) };

        uint64_t ret = create_pda(authority_account, &seed, 1, superuser_account->key, &(Constants.self_program_pubkey),
                                  get_rent_exempt_minimum(sizeof(Authority)), sizeof(Authority), params->ka,
                                  params->ka_num);
        if (ret) {
            return ret;
        }

        // Set the initial contents of the authority account.  A newly created account is all zeroes, so the escrow
        // total is already zero.
        Authority *authority = (Authority *) (authority_account->data);
        authority->data_type = DataType_Authority;
    }

    // Create the reference stake account, which MeasureRewardRate uses to measure the reward rate that a delegator to
    // Shinobi Systems earns.  It is funded by the superuser with REFERENCE_STAKE_ACCOUNT_LAMPORTS beyond its rent
    // exempt reserve, and both of its authorities are the authority account, so that only the program has any control
    // over it.  The program never withdraws from it.
    {
        const uint8_t *seed_bytes = (uint8_t *) Constants.reference_stake_seed_bytes;
        SolSignerSeed seed = { seed_bytes, sizeof(Constants.reference_stake_seed_bytes) };

        uint64_t ret = create_stake_account(reference_stake_account, &seed, 1, superuser_account->key,
                                            REFERENCE_STAKE_ACCOUNT_LAMPORTS, &(Constants.authority_pubkey),
                                            &(Constants.authority_pubkey), params->ka, params->ka_num);
        if (ret) {
            return ret;
        }
    }

    // Delegate the reference stake account to Shinobi Systems
    delegate_stake_signed(reference_stake_account->key, &(Constants.shinobi_systems_vote_pubkey), params->ka,
                          params->ka_num);

    // Create the Ki mint
    {
        const uint8_t *seed_bytes = (uint8_t *) Constants.ki_mint_seed_bytes;
        SolSignerSeed seed = { seed_bytes, sizeof(Constants.ki_mint_seed_bytes) };

        // The Ki mint has no freeze authority: Ki token accounts can never be frozen
        uint64_t ret = create_token_mint(ki_mint_account, &seed, 1, &(Constants.authority_pubkey),
                                         /* freeze_authority_key */ 0, superuser_account->key, 1, params->ka,
                                         params->ka_num);
        if (ret) {
            return ret;
        }
    }

    // Create the metadata for the Ki mint
    create_metaplex_metadata(&(Constants.ki_metadata_pubkey), &(Constants.ki_mint_pubkey), superuser_account->key,
                             (const uint8_t *) KI_TOKEN_NAME, (const uint8_t *) KI_TOKEN_SYMBOL,
                             (const uint8_t *) KI_TOKEN_METADATA_URI, &(Constants.system_program_pubkey),
                             &(Constants.system_program_pubkey), params->ka, params->ka_num);

    // Create the Shinobi Bid mint
    {
        const uint8_t *seed_bytes = (uint8_t *) Constants.bid_marker_mint_seed_bytes;
        SolSignerSeed seed = { seed_bytes, sizeof(Constants.bid_marker_mint_seed_bytes) };

        // The bid marker mint has no freeze authority: bid marker token accounts can never be frozen
        uint64_t ret = create_token_mint(bid_marker_mint_account, &seed, 1, &(Constants.authority_pubkey),
                                         /* freeze_authority_key */ 0, superuser_account->key, 1, params->ka,
                                         params->ka_num);
        if (ret) {
            return ret;
        }
    }

    // Create the metadata for the Bid Marker mint
    create_metaplex_metadata(&(Constants.bid_marker_metadata_pubkey), &(Constants.bid_marker_mint_pubkey),
                             superuser_account->key, (const uint8_t *) BID_MARKER_TOKEN_NAME,
                             (const uint8_t *) BID_MARKER_TOKEN_SYMBOL, (const uint8_t *) BID_MARKER_TOKEN_METADATA_URI,
                             &(Constants.system_program_pubkey), &(Constants.system_program_pubkey), params->ka,
                             params->ka_num);

    return 0;
}
