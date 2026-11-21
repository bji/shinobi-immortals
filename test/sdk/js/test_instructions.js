'use strict';

// Tests every instruction builder of sdk/js/shinobi_immortals_base.js against the transaction that the corresponding
// script in scripts/ produces for the same inputs.  The scripts are the reference for each instruction's accounts and
// data, and are what the program tests (test/program) use to exercise the program.
//
// Each script's transaction is decoded with "solxact decode", and its instruction compared with the builder's: the
// program, every account in order, whether each signs and is writable, and the data.  A transaction gives each
// address one set of permissions (the most that any use of it needs, and the fee payer always signs and is
// writable), so the builder's accounts are compared after combining their permissions in the same way.

const { Base, Results, random_bytes, random_int, random_bigint, random_address, require_tools, run_script,
        solxact_decode, make_entry, transaction_permissions, to_json } = require("./common.js");

const results = new Results("instructions");

require_tools(results.name, [ "bash", "solxact" ]);


// Compares the instruction that a builder made with the one in the transaction that a script makes
function compare(description, script, args, instruction)
{
    let decoded;
    try {
        decoded = solxact_decode(run_script(script, args));
    }
    catch (error) {
        results.check(description, false, "running " + script + " failed: " + error.message);
        return;
    }

    if (!results.check(description + ": the script makes one instruction", decoded.instructions.length == 1)) {
        return;
    }

    let expected_instruction = decoded.instructions[0];
    let expected = {
        program : expected_instruction.program_id,
        accounts : expected_instruction.addresses.map((a) => [ a.address, !!a.is_signed, !!a.is_read_write ]),
        data : Base.bytes_to_hex(expected_instruction.data)
    };

    // Combine the permissions of the builder's accounts as the transaction does, including the fee payer's
    let fee_payer = decoded.addresses.find((a) => a.fee_payer).address;
    let actual = {
        program : instruction.programAddress,
        accounts : transaction_permissions(instruction, fee_payer),
        data : Base.bytes_to_hex(instruction.data)
    };

    results.check(description + ": program", actual.program == expected.program);
    results.check(description + ": number of accounts", actual.accounts.length == expected.accounts.length,
                  actual.accounts.length + " != " + expected.accounts.length);
    for (let i = 0; i < Math.max(actual.accounts.length, expected.accounts.length); i++) {
        results.check(description + ": account " + i, to_json(actual.accounts[i]) == to_json(expected.accounts[i]),
                      "builder " + to_json(actual.accounts[i]) + ", script " + to_json(expected.accounts[i]));
    }
    results.check(description + ": data", actual.data == expected.data,
                  "builder " + actual.data + ", script " + expected.data);
}


// Random inputs
const SUPERUSER = random_address();
const ADMIN = random_address();
const USER = random_address();
const OTHER = random_address();
const STAKE = random_address();
const TOKEN = random_address();
const G = random_int(0x100000000);
const B = random_int(0x100000000);
const I = random_int(0x10000);
const ENTRY = make_entry(G, B, I, null);
const STAKED_ENTRY = make_entry(G, B, I, STAKE);
const U64_MAX = 18446744073709551615n;


// Superuser instructions ---------------------------------------------------------------------------------------------

compare("Initialize", "super_initialize_tx.sh", [ SUPERUSER, ADMIN, 650 ],
        Base.make_initialize_instruction(SUPERUSER, ADMIN, 650));

compare("SetAdmin", "super_set_admin_tx.sh", [ SUPERUSER, OTHER ], Base.make_set_admin_instruction(SUPERUSER, OTHER));


// Admin instructions -------------------------------------------------------------------------------------------------

// With an auction (and a whitelist), and without (and with no whitelist)
for (let [ auction_duration, whitelist_duration ] of [ [ random_int(0xFFFFFFFF) + 1, random_int(100000) + 1 ],
                                                      [ 0, 0 ] ]) {
    let config = {
        group_number : G, block_number : B, total_entry_count : random_int(0x10000),
        total_mystery_count : random_int(0x10000), mystery_phase_duration : random_int(0x100000000),
        mystery_start_price_lamports : random_bigint(64), reveal_period_duration : random_int(0x100000000),
        minimum_price_lamports : random_bigint(64), auction_duration : auction_duration,
        final_sale_duration : random_int(0x100000000), final_start_price_lamports : random_bigint(64),
        whitelist_duration : whitelist_duration
    };
    compare("CreateBlock (auction_duration " + auction_duration + ")", "admin_create_block_tx.sh",
            [ ADMIN, G, B, config.total_entry_count, config.total_mystery_count, config.mystery_phase_duration,
              config.mystery_start_price_lamports, config.reveal_period_duration, config.minimum_price_lamports,
              auction_duration, config.final_sale_duration, config.final_start_price_lamports, whitelist_duration ],
            Base.make_create_block_instruction(ADMIN, config));
}

let sha256s = [ 1, 2, 3 ].map(() => random_bytes(32).toString("hex"));
compare("AddEntriesToBlock", "admin_add_entries_to_block_tx.sh",
        [ ADMIN, G, B, "https://shinobi.example/initial.json", "none", I ].concat(sha256s),
        Base.make_add_entries_to_block_instruction(ADMIN, G, B, "https://shinobi.example/initial.json", null, I,
                                                   sha256s));
compare("AddEntriesToBlock (second creator, SHA-256s as bytes)", "admin_add_entries_to_block_tx.sh",
        [ ADMIN, G, B, "u", OTHER, 0, sha256s[0] ],
        Base.make_add_entries_to_block_instruction(ADMIN, G, B, "u", OTHER, 0, [ Base.hex_to_bytes(sha256s[0]) ]));
compare("AddEntriesToBlock (200 byte uri)", "admin_add_entries_to_block_tx.sh",
        [ ADMIN, G, B, "x".repeat(200), "none", 7 ].concat(sha256s.slice(0, 2)),
        Base.make_add_entries_to_block_instruction(ADMIN, G, B, "x".repeat(200), null, 7, sha256s.slice(0, 2)));

let metadata_bytes = random_bytes(1 + random_int(600));
let start_index = random_int(2000);
compare("SetMetadataBytes", "admin_set_metadata_bytes_tx.sh",
        [ ADMIN, G, B, I, start_index, metadata_bytes.toString("base64") ],
        Base.make_set_metadata_bytes_instruction(ADMIN, G, B, I, start_index, metadata_bytes));

let salts = [ random_bigint(64), U64_MAX, 0n ];
compare("RevealEntries", "admin_reveal_entries_tx.sh", [ ADMIN, G, B, I ].concat(salts),
        Base.make_reveal_entries_instruction(ADMIN, G, B, I, salts));

for (let count of [ 1, 3, Base.MAX_WHITELIST_ADD_ENTRIES ]) {
    let addresses = Array.from({ length : count }, random_address);
    compare("AddWhitelistEntries (" + count + ")", "admin_add_whitelist_entries_tx.sh",
            [ ADMIN, G, B ].concat(addresses), Base.make_add_whitelist_entries_instruction(ADMIN, G, B, addresses));
}

compare("DeleteWhitelist", "admin_delete_whitelist_tx.sh", [ ADMIN, G, B ],
        Base.make_delete_whitelist_instruction(ADMIN, G, B));

let block_address = Base.get_block_address(G, B);
const WithdrawExcessAccountType = Base.WithdrawExcessAccountType;
compare("WithdrawExcess (program state)", "admin_withdraw_excess_tx.sh", [ ADMIN, "state" ],
        Base.make_withdraw_excess_instruction(WithdrawExcessAccountType.ProgramState, null, ADMIN));
compare("WithdrawExcess (authority)", "admin_withdraw_excess_tx.sh", [ ADMIN, "authority" ],
        Base.make_withdraw_excess_instruction(WithdrawExcessAccountType.Authority, null, ADMIN));
compare("WithdrawExcess (block)", "admin_withdraw_excess_tx.sh", [ ADMIN, "block", G, B ],
        Base.make_withdraw_excess_instruction(WithdrawExcessAccountType.Block, block_address, ADMIN));
compare("WithdrawExcess (entry)", "admin_withdraw_excess_tx.sh", [ ADMIN, "entry", G, B, I ],
        Base.make_withdraw_excess_instruction(WithdrawExcessAccountType.Entry, Base.entry_address_of(ENTRY), ADMIN));
compare("WithdrawExcess (whitelist)", "admin_withdraw_excess_tx.sh", [ ADMIN, "whitelist", G, B ],
        Base.make_withdraw_excess_instruction(WithdrawExcessAccountType.Whitelist, block_address, ADMIN));


// User instructions --------------------------------------------------------------------------------------------------

let price = random_bigint(64);
compare("Buy", "user_buy_tx.sh", [ ADMIN, USER, G, B, I, price ],
        Base.make_buy_instruction(ENTRY, price, USER, ADMIN));

compare("Refund", "user_refund_tx.sh", [ USER, G, B, I ], Base.make_refund_instruction(ENTRY, USER));
compare("Refund (token account given)", "user_refund_tx.sh", [ USER, G, B, I, TOKEN ],
        Base.make_refund_instruction(ENTRY, USER, TOKEN));

let minimum_bid = random_bigint(63);
let maximum_bid = minimum_bid + random_bigint(62);
compare("Bid", "user_bid_tx.sh", [ USER, G, B, I, minimum_bid, maximum_bid ],
        Base.make_bid_instruction(ENTRY, minimum_bid, maximum_bid, USER));

// The script reclaims the bid marker only if asked to ("true"); the builder reclaims it unless asked not to
compare("ClaimLosing (reclaiming the bid marker)", "user_claim_losing_tx.sh", [ USER, G, B, I, "true" ],
        Base.make_claim_losing_instruction(ENTRY, USER));
compare("ClaimLosing (keeping the bid marker)", "user_claim_losing_tx.sh", [ USER, G, B, I ],
        Base.make_claim_losing_instruction(ENTRY, USER, true));

compare("Stake", "user_stake_tx.sh", [ USER, G, B, I, STAKE ], Base.make_stake_instruction(ENTRY, STAKE, USER));

for (let [ no_ki_harvest, no_thaw ] of [ [ false, false ], [ true, false ], [ false, true ], [ true, true ] ]) {
    let flags = (no_ki_harvest ? [ "no_ki_harvest" ] : [ ]).concat(no_thaw ? [ "no_thaw" ] : [ ]);
    compare("Destake (" + (flags.join(", ") || "no flags") + ")", "user_destake_tx.sh",
            [ USER, G, B, I, STAKE ].concat(flags),
            Base.make_destake_instruction(STAKED_ENTRY, USER, no_ki_harvest, no_thaw));
}

compare("Thaw", "user_thaw_tx.sh", [ USER, G, B, I ], Base.make_thaw_instruction(ENTRY, USER));

compare("Harvest", "user_harvest_tx.sh", [ USER, G, B, I, STAKE ], Base.make_harvest_instruction(STAKED_ENTRY, USER));

let ki = random_bigint(64);
compare("LevelUp (staked)", "user_level_up_tx.sh", [ USER, G, B, I, ki, STAKE ],
        Base.make_level_up_instruction(STAKED_ENTRY, ki, USER));
compare("LevelUp (not staked)", "user_level_up_tx.sh", [ USER, G, B, I, ki, "none" ],
        Base.make_level_up_instruction(ENTRY, ki, USER));


// Anyone instructions ------------------------------------------------------------------------------------------------

// The bid marker is reclaimed only when the bidder settles their own bid, and does not ask to keep it
compare("SettleWinningBid (by another account)", "anyone_settle_winning_bid_tx.sh", [ OTHER, ADMIN, USER, G, B, I ],
        Base.make_settle_winning_bid_instruction(ENTRY, USER, ADMIN, OTHER));
compare("SettleWinningBid (by the bidder)", "anyone_settle_winning_bid_tx.sh", [ USER, ADMIN, USER, G, B, I ],
        Base.make_settle_winning_bid_instruction(ENTRY, USER, ADMIN, USER));
compare("SettleWinningBid (by the bidder, keeping the bid marker)", "anyone_settle_winning_bid_tx.sh",
        [ USER, ADMIN, USER, G, B, I, "keep_bid_marker" ],
        Base.make_settle_winning_bid_instruction(ENTRY, USER, ADMIN, USER, true));

compare("Delegate", "anyone_delegate_tx.sh", [ OTHER, G, B, I, STAKE ], Base.make_delegate_instruction(STAKED_ENTRY));

compare("UpdateEpochsStaked (staked)", "anyone_update_epochs_staked_tx.sh", [ OTHER, G, B, I, STAKE ],
        Base.make_update_epochs_staked_instruction(STAKED_ENTRY));
compare("UpdateEpochsStaked (not staked)", "anyone_update_epochs_staked_tx.sh", [ OTHER, G, B, I, "none" ],
        Base.make_update_epochs_staked_instruction(ENTRY));

compare("MeasureRewardRate", "anyone_measure_reward_rate_tx.sh", [ OTHER ],
        Base.make_measure_reward_rate_instruction(false));
compare("MeasureRewardRate (require measured)", "anyone_measure_reward_rate_tx.sh", [ OTHER, "require_measured" ],
        Base.make_measure_reward_rate_instruction(true));


// Special instructions -----------------------------------------------------------------------------------------------

compare("ReAuthorize (metadata)", "special_reauthorize_tx.sh", [ OTHER, ADMIN, USER, G, B, I, STAKE, TOKEN, "true" ],
        Base.make_reauthorize_instruction(STAKED_ENTRY, ADMIN, USER, TOKEN, true));
compare("ReAuthorize (stake account only, token account given)", "special_reauthorize_tx.sh",
        [ OTHER, ADMIN, USER, G, B, I, STAKE, OTHER, "false", TOKEN ],
        Base.make_reauthorize_instruction(STAKED_ENTRY, ADMIN, USER, OTHER, false, TOKEN));


// Every instruction is covered ---------------------------------------------------------------------------------------

// Each builder appears above; this makes sure that a newly added builder is not left untested
const TESTED_BUILDERS = [
    "initialize", "set_admin", "create_block", "add_entries_to_block", "set_metadata_bytes", "reveal_entries",
    "add_whitelist_entries", "delete_whitelist", "withdraw_excess", "buy", "refund", "bid", "claim_losing", "stake",
    "destake", "thaw", "harvest", "level_up", "settle_winning_bid", "delegate", "update_epochs_staked",
    "measure_reward_rate", "reauthorize"
].map((name) => "make_" + name + "_instruction");
for (let builder of Object.keys(Base).filter((k) => /^make_\w+_instruction$/.test(k))) {
    results.check(builder + " is tested", TESTED_BUILDERS.includes(builder));
}


// Input validation ---------------------------------------------------------------------------------------------------

results.check_throws("AddEntriesToBlock rejects a uri longer than 200 bytes",
                     () => Base.make_add_entries_to_block_instruction(ADMIN, G, B, "x".repeat(201), null, 0, sha256s),
                     "longer than 200");
results.check_throws("AddEntriesToBlock rejects an invalid SHA-256",
                     () => Base.make_add_entries_to_block_instruction(ADMIN, G, B, "u", null, 0, [ "abc" ]),
                     "Invalid SHA-256");
results.check_throws("AddWhitelistEntries rejects no addresses",
                     () => Base.make_add_whitelist_entries_instruction(ADMIN, G, B, [ ]), "may be added at once");
results.check_throws("AddWhitelistEntries rejects too many addresses",
                     () => Base.make_add_whitelist_entries_instruction(
                         ADMIN, G, B, Array.from({ length : Base.MAX_WHITELIST_ADD_ENTRIES + 1 }, random_address)),
                     "may be added at once");
results.check_throws("ReAuthorize of metadata requires a new metadata authority",
                     () => Base.make_reauthorize_instruction(STAKED_ENTRY, ADMIN, USER, null, true),
                     "new metadata authority is required");
results.check_throws("Buy rejects a negative price",
                     () => Base.make_buy_instruction(ENTRY, -1, USER, ADMIN), "out of u64 range");
results.check_throws("Buy rejects a price beyond u64",
                     () => Base.make_buy_instruction(ENTRY, U64_MAX + 1n, USER, ADMIN), "out of u64 range");
results.check_throws("Buy rejects an invalid address", () => Base.make_buy_instruction(ENTRY, 1, "x", ADMIN),
                     "Invalid");

// A builder accepts a plain decoded entry, or an entry with its own address and block (as an Entry of
// shinobi_immortals.js has), and gives the same instruction either way
let entry_with_addresses = Object.assign({ address : Base.entry_address_of(ENTRY),
                                           block : { address : block_address } }, ENTRY);
results.check("a builder gives the same instruction for an entry with or without its own addresses",
              to_json(Base.make_buy_instruction(ENTRY, 5, USER, ADMIN)) ==
              to_json(Base.make_buy_instruction(entry_with_addresses, 5, USER, ADMIN)));


results.finish();
