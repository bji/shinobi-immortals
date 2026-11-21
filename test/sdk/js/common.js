'use strict';

// Shared helpers for the tests of the Shinobi Immortals Javascript SDK (sdk/js).  See README.txt in this directory.

const crypto = require("crypto");
const path = require("path");
const { execFileSync } = require("child_process");

// The root of the source tree: this file is in test/sdk/js
const SOURCE_ROOT = path.resolve(__dirname, "..", "..", "..");

// The modules under test
const SDK_DIR = path.join(SOURCE_ROOT, "sdk", "js");
const Base = require(path.join(SDK_DIR, "shinobi_immortals_base.js"));


// Results ------------------------------------------------------------------------------------------------------------

// Collects the results of a test file's checks, printing each as it is made, in the same PASS/FAIL form as the
// program tests.  finish() prints a summary and exits with a nonzero status if any check failed.
class Results
{
    constructor(name)
    {
        this.name = name;
        this.passed = 0;
        this.failed = 0;
    }

    // Records one check: ok is whether it passed, and detail (optional) describes a failure
    check(description, ok, detail)
    {
        if (ok) {
            this.passed += 1;
            if (process.env.VERBOSE) {
                console.log("PASS: " + this.name + ": " + description);
            }
        }
        else {
            this.failed += 1;
            console.log("FAIL: " + this.name + ": " + description + ((detail == null) ? "" : (": " + detail)));
        }
        return ok;
    }

    // Records a check that fn does not throw, and that what it returns is truthy
    check_no_throw(description, fn)
    {
        try {
            return this.check(description, fn());
        }
        catch (error) {
            return this.check(description, false, "threw " + error.message);
        }
    }

    // Records a check that fn throws an error whose message includes message_part
    check_throws(description, fn, message_part)
    {
        try {
            fn();
        }
        catch (error) {
            return this.check(description, error.message.includes(message_part),
                              "threw \"" + error.message + "\", expected a message including \"" + message_part + "\"");
        }
        return this.check(description, false, "did not throw");
    }

    finish()
    {
        if (this.failed) {
            console.log("FAIL: " + this.name + ": " + this.failed + " of " + (this.passed + this.failed) +
                        " checks failed (seed " + SEED + ")");
            process.exit(1);
        }
        console.log("PASS: " + this.name + ": " + this.passed + " checks");
    }
}


// Random values ------------------------------------------------------------------------------------------------------
//
// Random inputs come from a seeded generator, so that a failure can be reproduced by running the test again with the
// seed that it prints: TEST_SEED=<seed> node <test file>.  The generator is SHA-256 in counter mode, which is plenty
// for test inputs.

const SEED = process.env.TEST_SEED || crypto.randomBytes(8).toString("hex");
let g_random_counter = 0;


// Returns a Buffer of n random bytes
function random_bytes(n)
{
    let chunks = [ ];
    let total = 0;
    while (total < n) {
        let chunk = crypto.createHash("sha256").update(SEED + ":" + (g_random_counter++)).digest();
        chunks.push(chunk);
        total += chunk.length;
    }
    return Buffer.concat(chunks).subarray(0, n);
}


// Returns a random integer from 0 to max - 1 (max at most 2^32)
function random_int(max)
{
    return random_bytes(4).readUInt32LE(0) % max;
}


// Returns a random BigInt from 0 to 2^bits - 1
function random_bigint(bits)
{
    return BigInt.asUintN(bits, random_bytes(8).readBigUInt64LE(0));
}


// Returns a random address
function random_address()
{
    return Base.base58_encode(random_bytes(32));
}


// External tools -----------------------------------------------------------------------------------------------------


// Returns true if the named program can be found on the PATH
function have_tool(name)
{
    try {
        execFileSync("sh", [ "-c", "command -v " + name ], { stdio : "ignore" });
        return true;
    }
    catch (error) {
        return false;
    }
}


// Exits with an error if any of the named programs is not on the PATH
function require_tools(results_name, names)
{
    let missing = names.filter((name) => !have_tool(name));
    if (missing.length) {
        console.log("FAIL: " + results_name + ": required program(s) not found: " + missing.join(", "));
        process.exit(1);
    }
}


// Returns the lines that the reference program name (of reference/, a Rust program built with cargo) prints for this
// run's seed.  Each prints test vectors whose expected results are computed by Solana's own crates; see
// reference/Cargo.toml.  The first run builds the programs (and, if they are not already in cargo's cache, downloads
// the crates that Cargo.lock pins); later runs only run them.
function reference_vectors(name)
{
    let output = execFileSync("cargo", [ "run", "--quiet", "--release", "--locked",
                                         "--manifest-path", path.join(__dirname, "reference", "Cargo.toml"),
                                         "--bin", name, "--", SEED ],
                              { stdio : [ "ignore", "pipe", "inherit" ], maxBuffer : 1 << 26 });
    return output.toString().trim().split("\n");
}


// Runs the script scripts/<name> with the given arguments, returning what it writes to stdout as a Buffer.  The
// transaction scripts write an unsigned transaction.
function run_script(name, args)
{
    return execFileSync("bash", [ path.join(SOURCE_ROOT, "scripts", name) ].concat(args.map(String)),
                        { maxBuffer : 1 << 24 });
}


// Returns the JSON that "solxact decode" makes of the transaction tx (a Buffer)
function solxact_decode(tx)
{
    return JSON.parse(execFileSync("solxact", [ "decode" ], { input : tx, maxBuffer : 1 << 24 }).toString());
}


// Returns [ address, bump_seed ]: the program derived address that "solxact pda" computes for program_address and
// the seeds, which are given in the solxact data value format (for example [ "u8", "4", "pubkey", address ])
function solxact_pda(program_address, seeds)
{
    let output = execFileSync("solxact", [ "pda", program_address, "[" ].concat(seeds.map(String), [ "]" ]))
        .toString().trim();
    let dot = output.lastIndexOf(".");
    return [ output.substring(0, dot), parseInt(output.substring(dot + 1)) ];
}


// Packages -----------------------------------------------------------------------------------------------------------
//
// test_kit.js, test_rpc.js, and test_wallet.js use @solana/kit from npm (see package.json), installed in
// node_modules here by "make sdk-js-packages" (which runs "npm ci --ignore-scripts").  The other tests use no
// packages.

const PACKAGES_DIR = path.join(__dirname, "node_modules");


// If the packages are installed, makes them available to require(), both here and in sdk/js/shinobi_immortals.js
// (which is outside of this directory, so would not otherwise find them), and returns true.  Otherwise prints that
// the test is skipped and returns false.
function use_packages(results_name)
{
    if (!require("fs").existsSync(PACKAGES_DIR)) {
        console.log("SKIP: " + results_name + ": its packages are not installed (run \"make sdk-js-packages\")");
        return false;
    }

    // Module search paths are fixed when Node starts, so add this directory to them and have Node recompute them
    process.env.NODE_PATH = [ PACKAGES_DIR ].concat(process.env.NODE_PATH ? [ process.env.NODE_PATH ] : [ ])
        .join(path.delimiter);
    require("module").Module._initPaths();
    return true;
}


// Sample instructions ------------------------------------------------------------------------------------------------


// Returns an entry, as decode_entry would return it, for an entry of a block; the builders take entries in this form.
// stake_account is the entry's stake account if it is staked, or null if it is not.
function make_entry(group_number, block_number, entry_index, stake_account)
{
    let mint_address = Base.get_entry_mint_address(Base.get_block_address(group_number, block_number), entry_index);
    return {
        group_number : group_number,
        block_number : block_number,
        entry_index : entry_index,
        mint_address : mint_address,
        token_address : Base.get_entry_token_address(mint_address),
        metaplex_metadata_address : Base.get_metaplex_metadata_address(mint_address),
        owned_stake_account : (stake_account == null) ? Base.g_system_program_address : stake_account
    };
}


// Returns [ { name, signer, instruction } ]: an instruction made by every builder (several, for builders whose
// options change the accounts), with random inputs, and the address of an account that signs it (to be the fee
// payer)
function sample_instructions()
{
    let superuser = random_address(), admin = random_address(), user = random_address(), other = random_address();
    let stake = random_address(), token = random_address();
    let g = random_int(0x100000000), b = random_int(0x100000000), i = random_int(0x10000);
    let entry = make_entry(g, b, i, null);
    let staked_entry = make_entry(g, b, i, stake);
    let block_address = Base.get_block_address(g, b);
    let config = {
        group_number : g, block_number : b, total_entry_count : 10, total_mystery_count : 5,
        mystery_phase_duration : 100, mystery_start_price_lamports : random_bigint(64), reveal_period_duration : 200,
        minimum_price_lamports : random_bigint(64), auction_duration : 300, final_sale_duration : 0,
        final_start_price_lamports : random_bigint(64), whitelist_duration : 0
    };
    let sha256s = [ random_bytes(32), random_bytes(32) ];
    const T = Base.WithdrawExcessAccountType;

    return [
        [ "Initialize", superuser, Base.make_initialize_instruction(superuser, admin, 650) ],
        [ "SetAdmin", superuser, Base.make_set_admin_instruction(superuser, other) ],
        [ "CreateBlock", admin, Base.make_create_block_instruction(admin, config) ],
        [ "CreateBlock (funded by another)", other, Base.make_create_block_instruction(admin, config, other) ],
        [ "AddEntriesToBlock", admin, Base.make_add_entries_to_block_instruction(admin, g, b, "u", null, i, sha256s) ],
        [ "SetMetadataBytes", admin, Base.make_set_metadata_bytes_instruction(admin, g, b, i, 7, random_bytes(100)) ],
        [ "RevealEntries", admin, Base.make_reveal_entries_instruction(admin, g, b, i, [ random_bigint(64), 1n ]) ],
        [ "AddWhitelistEntries", admin,
          Base.make_add_whitelist_entries_instruction(admin, g, b, [ random_address(), random_address() ]) ],
        [ "DeleteWhitelist", admin, Base.make_delete_whitelist_instruction(admin, g, b) ],
        [ "WithdrawExcess (program state)", admin, Base.make_withdraw_excess_instruction(T.ProgramState, null, admin) ],
        [ "WithdrawExcess (whitelist)", admin,
          Base.make_withdraw_excess_instruction(T.Whitelist, block_address, admin) ],
        [ "Buy", user, Base.make_buy_instruction(entry, random_bigint(64), user, admin) ],
        [ "Refund", user, Base.make_refund_instruction(entry, user, token) ],
        [ "Bid", user, Base.make_bid_instruction(entry, 1000n, random_bigint(64), user) ],
        [ "ClaimLosing", user, Base.make_claim_losing_instruction(entry, user) ],
        [ "ClaimLosing (keeping the bid marker)", user, Base.make_claim_losing_instruction(entry, user, true) ],
        [ "Stake", user, Base.make_stake_instruction(entry, stake, user) ],
        [ "Destake", user, Base.make_destake_instruction(staked_entry, user, false, true) ],
        [ "Thaw", user, Base.make_thaw_instruction(entry, user) ],
        [ "Harvest", user, Base.make_harvest_instruction(staked_entry, user) ],
        [ "LevelUp", user, Base.make_level_up_instruction(staked_entry, random_bigint(64), user) ],
        [ "SettleWinningBid", other, Base.make_settle_winning_bid_instruction(entry, user, admin, other) ],
        [ "SettleWinningBid (by the bidder)", user,
          Base.make_settle_winning_bid_instruction(entry, user, admin, user) ],
        [ "Delegate", other, Base.make_delegate_instruction(staked_entry) ],
        [ "UpdateEpochsStaked", other, Base.make_update_epochs_staked_instruction(staked_entry) ],
        [ "MeasureRewardRate", other, Base.make_measure_reward_rate_instruction(true) ],
        [ "ReAuthorize", user, Base.make_reauthorize_instruction(staked_entry, admin, user, other, true) ]
    ].map(([ name, signer, instruction ]) => ({ name, signer, instruction }));
}


// Returns, for each account of an instruction, [ address, signs, writable ] with the permissions that a transaction
// gives it: each address gets the most that any use of it in the instruction needs, and fee_payer (if given) always
// signs and is writable
function transaction_permissions(instruction, fee_payer)
{
    let roles = new Map((fee_payer == null) ? [ ] : [ [ fee_payer, Base.AccountRole.WRITABLE_SIGNER ] ]);
    for (let account of instruction.accounts) {
        roles.set(account.address, (roles.get(account.address) || 0) | account.role);
    }
    return instruction.accounts.map((a) => [ a.address, (roles.get(a.address) & 2) != 0,
                                             (roles.get(a.address) & 1) != 0 ]);
}


// Mock cluster -------------------------------------------------------------------------------------------------------
//
// Tests of sdk/js/shinobi_immortals.js use @solana/kit's RPC client with a mock transport in place of the network, so
// that requests and responses go through @solana/kit exactly as they would with an RPC server (including its
// conversion of numbers in responses to BigInts).


// Returns { rpc, requests }: a @solana/kit RPC client whose requests are answered by handlers, which maps each RPC
// method name to a function of the request's params that returns the result, in the JSON form that an RPC server
// would return.  A handler can instead make the request fail with a JSON-RPC error, by throwing an object with an
// rpc_error property holding the error ({ code, message, data }).  Each request's payload is recorded in requests.  A
// request for a method with no handler fails.
function make_mock_rpc(handlers)
{
    let Kit = require("@solana/kit");
    let requests = [ ];
    let transport = async ({ payload }) => {
        requests.push(payload);
        let handler = handlers[payload.method];
        if (handler == null) {
            return { jsonrpc : "2.0", id : payload.id,
                     error : { code : -32601, message : "mock RPC has no " + payload.method } };
        }
        try {
            return { jsonrpc : "2.0", id : payload.id, result : await handler(payload.params) };
        }
        catch (error) {
            if ((error != null) && (error.rpc_error != null)) {
                return { jsonrpc : "2.0", id : payload.id, error : error.rpc_error };
            }
            throw error;
        }
    };
    return { rpc : Kit.createSolanaRpcFromTransport(transport), requests : requests };
}


// Returns an RpcConnections of sdk/js/shinobi_immortals.js with a single RpcConnection, which uses rpc (from
// make_mock_rpc) in place of an RPC server
function make_mock_rpc_connections(SDK, rpc)
{
    let rpc_connection = SDK.RpcConnection.create("http://mock");
    rpc_connection.rpc = rpc;
    let rpc_connections = SDK.RpcConnections.create();
    rpc_connections.map = new Map([ [ "http://mock", rpc_connection ] ]);
    rpc_connections.array = [ rpc_connection ];
    rpc_connections.index = 0;
    return rpc_connections;
}


// Returns a new ed25519 key pair as { address, bytes }, where bytes is the 64 byte form used by Solana key files
// (the private key's 32 byte seed, then the public key)
function make_key_pair()
{
    let { privateKey, publicKey } = crypto.generateKeyPairSync("ed25519");
    let seed = Buffer.from(privateKey.export({ format : "jwk" }).d, "base64url");
    let public_key = Buffer.from(publicKey.export({ format : "jwk" }).x, "base64url");
    return { address : Base.base58_encode(public_key), bytes : Buffer.concat([ seed, public_key ]) };
}


// Formatting ---------------------------------------------------------------------------------------------------------


// JSON.stringify, with BigInts written as strings, for comparing and showing values
function to_json(value)
{
    return JSON.stringify(value, (key, v) => ((typeof(v) == "bigint") ? v.toString() : v));
}


module.exports = {
    SOURCE_ROOT, SDK_DIR, Base, Results, SEED,
    random_bytes, random_int, random_bigint, random_address,
    have_tool, require_tools, reference_vectors, run_script, solxact_decode, solxact_pda, use_packages, make_entry,
    sample_instructions, transaction_permissions, make_mock_rpc, make_mock_rpc_connections, make_key_pair, to_json
};
