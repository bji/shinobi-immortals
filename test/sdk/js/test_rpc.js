'use strict';

// Tests how sdk/js/shinobi_immortals.js reads the cluster, against a mock RPC server (see make_mock_rpc in common.js)
// holding a small cluster state in the JSON forms that an RPC server returns:
//
// - Each method of RpcConnection makes the right RPC request, and returns its result in the documented form
// - Cluster finds blocks and entries, keeps the clock, and reads the authority account and stake accounts
// - Entry computes its harvestable Ki from its stake account
// - Wallet finds its balance, Ki, entries, bids, and stake accounts
//
// This needs @solana/kit from npm (see package.json); it is skipped if that is not installed.

const path = require("path");
const { SDK_DIR, Base, Results, random_bytes, random_address, use_packages, make_mock_rpc,
        make_mock_rpc_connections, to_json } = require("./common.js");

const results = new Results("rpc");

if (!use_packages(results.name)) {
    process.exit(0);
}

const SDK = require(path.join(SDK_DIR, "shinobi_immortals.js"));


// The cluster state --------------------------------------------------------------------------------------------------

const U64_MAX = "18446744073709551615";
const WALLET = random_address();
const GENESIS_HASH = random_address();
const EPOCH = 300;
const SLOT = 129600000;
const BLOCK_TIME = 1760000000;

// One complete block, of group 0 and block 0, with one entry: its data is random but for the fields that matter here
const BLOCK_ADDRESS = Base.get_block_address(0, 0);
const block_data = random_bytes(96);
block_data.writeUInt16LE(1, 60);                                   // total_entry_count
block_data.writeUInt16LE(1, 64);                                   // added_entries_count
block_data.writeBigInt64LE(BigInt(BLOCK_TIME - (10 * 86400)), 72); // block_start_timestamp: 10 days ago

const ENTRY_MINT = Base.get_entry_mint_address(BLOCK_ADDRESS, 0);
const ENTRY_ADDRESS = Base.get_entry_address(ENTRY_MINT);
const STAKE_ACCOUNT = random_address();
const entry_data = random_bytes(3192);
entry_data.set(Base.address_to_bytes(ENTRY_MINT), 46);                 // mint
entry_data.set(Base.address_to_bytes(Base.get_metaplex_metadata_address(ENTRY_MINT)), 110);
entry_data.fill(0, 168, 200);                                          // revealed
entry_data.writeBigUInt64LE(5000000000n, 208);                         // purchase price
entry_data[217] = 0;                                                   // not reauthorized
entry_data.set(Base.address_to_bytes(STAKE_ACCOUNT), 264);             // staked
entry_data.writeBigUInt64LE(1000000000000n, 296);                      // stake at the last harvest
entry_data[304] = 2;                                                   // level
entry_data.writeUInt32LE(1000, 384 + (2 * 308));                       // level 2's ki_factor
entry_data.writeUInt32LE(4294967, 3148);                               // Ki harvest reduction per day (0.1%)

// The entry's stake account: delegated to Shinobi Systems since an earlier epoch, having earned 2 SOL since the last
// harvest
const STAKE_LAMPORTS = 1002000000000n;
const stake_data = Buffer.alloc(200);
stake_data.writeUInt32LE(2, 0);                                        // the Stake state
stake_data.set(Base.address_to_bytes(Base.g_shinobi_systems_vote_address), 124);
stake_data.writeBigUInt64LE(STAKE_LAMPORTS, 156);                      // delegated stake
stake_data.writeBigUInt64LE(BigInt(EPOCH - 10), 164);                  // activation epoch

// The wallet's own stake account, delegated to a validator whose name is published
const OWN_STAKE_ACCOUNT = random_address();
const VOTE_ACCOUNT = random_address();
const VALIDATOR_IDENTITY = random_address();
const vote_data = Buffer.alloc(100);
vote_data.set(Base.address_to_bytes(VALIDATOR_IDENTITY), 4);

// The wallet's token accounts: the entry's token (frozen, because it is staked), Ki, and a bid marker
const BID_ENTRY_MINT = Base.get_entry_mint_address(BLOCK_ADDRESS, 1);
const BID_MARKER_TOKEN = Base.get_bid_marker_token_address(BID_ENTRY_MINT, WALLET);
const BID_ADDRESS = Base.get_bid_address(BID_MARKER_TOKEN);
const bid_data = Buffer.alloc(68);
bid_data.writeUInt32LE(4, 0);
bid_data.set(Base.address_to_bytes(BID_ENTRY_MINT), 4);

// The entry's Metaplex metadata, whose update authority (at offset 1) is the program authority
const metadata_data = Buffer.alloc(679);
metadata_data[0] = 4;
metadata_data.set(Base.address_to_bytes(Base.g_program_authority_address), 1);

// The authority account: data type 6, and 7 SOL held in escrow, with 1 SOL more than its rent exempt minimum and that
const authority_data = Buffer.alloc(16);
authority_data.writeUInt32LE(6, 0);
authority_data.writeBigUInt64LE(7000000000n, 8);
const RENT_EXEMPT_MINIMUM = 1002240;

// Accounts by address: [ lamports, owner, data ]
const ACCOUNTS = new Map([
    [ BLOCK_ADDRESS, [ 2000000, Base.g_self_program_address, block_data ] ],
    [ ENTRY_ADDRESS, [ 23000000, Base.g_self_program_address, entry_data ] ],
    [ STAKE_ACCOUNT, [ Number(STAKE_LAMPORTS) + 2282880, Base.g_stake_program_address, stake_data ] ],
    [ VOTE_ACCOUNT, [ 27074400, "Vote111111111111111111111111111111111111111", vote_data ] ],
    [ BID_ADDRESS, [ 3500000000, Base.g_self_program_address, bid_data ] ],
    [ Base.get_metaplex_metadata_address(ENTRY_MINT), [ 5616720, Base.g_metaplex_program_address, metadata_data ] ],
    [ Base.g_program_authority_address, [ RENT_EXEMPT_MINIMUM + 8000000000, Base.g_self_program_address,
                                          authority_data ] ]
]);


// Returns an account of ACCOUNTS as an RPC server returns it with base64 encoding, with only the bytes of data_slice
// if it is given; or null if there is no such account
function base64_account(address, data_slice)
{
    let account = ACCOUNTS.get(address);
    if (account == null) {
        return null;
    }
    let [ lamports, owner, data ] = account;
    if (data_slice != null) {
        data = data.subarray(data_slice.offset, data_slice.offset + data_slice.length);
    }
    return { data : [ Buffer.from(data).toString("base64"), "base64" ], executable : false, lamports : lamports,
             owner : owner, rentEpoch : U64_MAX, space : account[2].length };
}


// A stake account as an RPC server returns it with jsonParsed encoding
function parsed_stake_account(lamports, voter, delegated_lamports, withdrawer, lockup_epoch)
{
    return {
        data : {
            program : "stake",
            parsed : {
                type : "delegated",
                info : {
                    meta : {
                        rentExemptReserve : "2282880",
                        authorized : { staker : withdrawer, withdrawer : withdrawer },
                        lockup : { unixTimestamp : 0, epoch : lockup_epoch, custodian : Base.g_system_program_address }
                    },
                    stake : {
                        delegation : { voter : voter, stake : String(delegated_lamports), activationEpoch : "200",
                                       deactivationEpoch : U64_MAX, warmupCooldownRate : 0.25 },
                        creditsObserved : 12345
                    }
                }
            },
            space : 200
        },
        executable : false, lamports : lamports, owner : Base.g_stake_program_address, rentEpoch : U64_MAX, space : 200
    };
}


// A token account as an RPC server returns it with jsonParsed encoding
function parsed_token_account(mint, amount, state)
{
    return {
        data : {
            program : "spl-token",
            parsed : {
                type : "account",
                info : { isNative : false, mint : mint, owner : WALLET, state : state,
                         tokenAmount : { amount : String(amount), decimals : 0, uiAmount : Number(amount),
                                         uiAmountString : String(amount) } }
            },
            space : 165
        },
        executable : false, lamports : 2039280, owner : Base.g_spl_token_program_address, rentEpoch : U64_MAX,
        space : 165
    };
}


const mock = make_mock_rpc({
    getGenesisHash : () => GENESIS_HASH,
    getEpochInfo : () => ({ absoluteSlot : SLOT, blockHeight : SLOT - 1000, epoch : EPOCH, slotIndex : 1000,
                            slotsInEpoch : 432000, transactionCount : 5 }),
    getBlockTime : ([ slot ]) => BLOCK_TIME,
    getBlockHeight : () => SLOT - 1000,
    // A balance beyond 2^53: @solana/kit parses such a value in a response as a BigInt, so the mock gives it as one
    getBalance : ([ address ]) => ({ context : { slot : SLOT }, value : (address == WALLET) ? 9007199254740993n : 0 }),
    getMinimumBalanceForRentExemption : ([ size ]) => (size == 16) ? RENT_EXEMPT_MINIMUM : 0,
    getLatestBlockhash : () => ({ context : { slot : SLOT }, value : { blockhash : random_address(),
                                                                        lastValidBlockHeight : SLOT } }),
    sendTransaction : () => random_address(),
    getAccountInfo : ([ address, config ]) => {
        if (config.encoding == "jsonParsed") {
            return { context : { slot : SLOT },
                     value : (address == STAKE_ACCOUNT) ?
                         parsed_stake_account(Number(STAKE_LAMPORTS) + 2282880, Base.g_shinobi_systems_vote_address,
                                              STAKE_LAMPORTS, Base.g_program_authority_address, 0) : null };
        }
        return { context : { slot : SLOT }, value : base64_account(address, config.dataSlice) };
    },
    getMultipleAccounts : ([ addresses ]) => ({ context : { slot : SLOT },
                                                value : addresses.map((a) => base64_account(a)) }),
    getTokenAccountsByOwner : ([ owner ]) => ({
        context : { slot : SLOT },
        value : (owner != WALLET) ? [ ] : [
            { pubkey : Base.get_associated_token_address(WALLET, ENTRY_MINT),
              account : parsed_token_account(ENTRY_MINT, 1, "frozen") },
            { pubkey : Base.get_associated_token_address(WALLET, Base.g_ki_mint_address),
              account : parsed_token_account(Base.g_ki_mint_address, "123456789012345678", "initialized") },
            { pubkey : BID_MARKER_TOKEN, account : parsed_token_account(Base.g_bid_marker_mint_address, 1,
                                                                        "initialized") },
            // An empty token account is ignored
            { pubkey : random_address(), account : parsed_token_account(random_address(), 0, "initialized") }
        ]
    }),
    getProgramAccounts : ([ program, config ]) => {
        if (program == Base.g_stake_program_address) {
            return [
                { pubkey : OWN_STAKE_ACCOUNT, account : parsed_stake_account(5002282880, VOTE_ACCOUNT, 5000000000,
                                                                             WALLET, 0) },
                // A stake account with a lockup is ignored
                { pubkey : random_address(), account : parsed_stake_account(5002282880, VOTE_ACCOUNT, 5000000000,
                                                                            WALLET, 500) }
            ];
        }
        if (program == Base.g_config_program_address) {
            return [ {
                pubkey : random_address(),
                account : {
                    data : { program : "config",
                             parsed : { type : "validatorInfo",
                                        info : { keys : [ { pubkey : Base.g_validator_info_address, signer : false },
                                                          { pubkey : VALIDATOR_IDENTITY, signer : true } ],
                                                 configData : { name : "Example Validator" } } },
                             space : 500 },
                    executable : false, lamports : 3000000, owner : Base.g_config_program_address,
                    rentEpoch : U64_MAX, space : 500
                }
            } ];
        }
        return [ ];
    }
});


// Returns the requests made of the mock RPC server since the call to this that returned mark
function requests_since(mark)
{
    return mock.requests.slice(mark);
}


async function main()
{
    let rpc_connections = make_mock_rpc_connections(SDK, mock.rpc);
    let connection = rpc_connections.array[0];


    // RpcConnection --------------------------------------------------------------------------------------------------

    let mark = mock.requests.length;
    results.check("get_genesis_hash", (await connection.get_genesis_hash()) == GENESIS_HASH);

    let account = await connection.get_account(BLOCK_ADDRESS);
    results.check("get_account gives Uint8Array data", (account.data instanceof Uint8Array) &&
                  (Base.bytes_to_hex(account.data) == block_data.toString("hex")));
    results.check("get_account gives BigInt lamports", account.lamports === 2000000n);
    results.check("get_account gives the owner", account.owner == Base.g_self_program_address);
    results.check("get_account of no account gives null", (await connection.get_account(random_address())) === null);
    let slice = await connection.get_account(VOTE_ACCOUNT, { offset : 4, length : 32 });
    results.check("get_account with a data slice gives those bytes",
                  Base.base58_encode(slice.data) == VALIDATOR_IDENTITY);
    let multiple = await connection.get_multiple_accounts([ ENTRY_ADDRESS, random_address() ]);
    results.check("get_multiple_accounts gives each account, or null",
                  (multiple.length == 2) && (multiple[1] === null) &&
                  (Base.bytes_to_hex(multiple[0].data) == entry_data.toString("hex")));
    let parsed = await connection.get_parsed_account(STAKE_ACCOUNT);
    results.check("get_parsed_account gives the parsed data",
                  (parsed.data.parsed.type == "delegated") && (typeof(parsed.lamports) == "bigint"));
    let epoch_info = await connection.get_epoch_info();
    results.check("get_epoch_info gives BigInts", (epoch_info.epoch === BigInt(EPOCH)) &&
                  (epoch_info.absoluteSlot === BigInt(SLOT)));
    results.check("get_block_time", (await connection.get_block_time(SLOT)) === BigInt(BLOCK_TIME));
    results.check("get_block_height", (await connection.get_block_height()) === BigInt(SLOT - 1000));
    // A balance beyond 2^53 survives exactly
    results.check("get_balance gives an exact BigInt", (await connection.get_balance(WALLET)) === 9007199254740993n);
    results.check("get_minimum_balance_for_rent_exemption",
                  (await connection.get_minimum_balance_for_rent_exemption(16)) === BigInt(RENT_EXEMPT_MINIMUM));
    let latest = await connection.get_latest_blockhash();
    results.check("get_latest_blockhash", (typeof(latest.blockhash) == "string") &&
                  (latest.lastValidBlockHeight === BigInt(SLOT)));
    let tokens = await connection.get_parsed_token_accounts_by_owner(WALLET, Base.g_spl_token_program_address);
    results.check("get_parsed_token_accounts_by_owner gives { address, account }",
                  (tokens.length == 4) && (typeof(tokens[0].address) == "string") &&
                  (tokens[0].account.data.parsed.info.mint == ENTRY_MINT));
    let program_accounts = await connection.get_parsed_program_accounts(Base.g_stake_program_address, [ ]);
    results.check("get_parsed_program_accounts gives { address, account }",
                  (program_accounts.length == 2) && (program_accounts[0].address == OWN_STAKE_ACCOUNT));
    results.check("send_transaction gives the signature",
                  typeof(await connection.send_transaction(Buffer.from("x").toString("base64"))) == "string");

    // The requests made: the right methods, with the documented encodings
    let requests = requests_since(mark);
    let request = (method, index) => requests.filter((r) => r.method == method)[index || 0];
    results.check("get_account requests base64 encoding", request("getAccountInfo").params[1].encoding == "base64");
    results.check("get_account with a data slice requests it",
                  to_json(request("getAccountInfo", 2).params[1].dataSlice) == to_json({ offset : 4, length : 32 }));
    results.check("get_parsed_account requests jsonParsed encoding",
                  request("getAccountInfo", 3).params[1].encoding == "jsonParsed");
    results.check("get_parsed_token_accounts_by_owner requests the token program's accounts",
                  (request("getTokenAccountsByOwner").params[1].programId == Base.g_spl_token_program_address) &&
                  (request("getTokenAccountsByOwner").params[2].encoding == "jsonParsed"));
    results.check("send_transaction sends base64", request("sendTransaction").params[1].encoding == "base64");


    // Cluster --------------------------------------------------------------------------------------------------------

    let new_entries = [ ];
    let update_complete;
    let updated = new Promise((resolve) => { update_complete = resolve; });
    let cluster = SDK.Cluster.create(rpc_connections, null, null, {
        on_new_entry : (entry) => new_entries.push(entry),
        on_entries_update_complete : () => update_complete()
    });

    try {
        // The cluster finds the block and its entry as soon as it starts
        await updated;
        results.check("Cluster finds the entry", (cluster.entry_count() == 1) && (new_entries.length == 1));
        let entry = cluster.get_entry(ENTRY_ADDRESS);
        results.check("Cluster decodes the entry", (entry != null) && (entry.mint_address == ENTRY_MINT));
        results.check("Cluster decodes the entry's block", (entry != null) && (entry.block.address == BLOCK_ADDRESS));

        // The clock, as Numbers
        let clock = cluster.get_clock();
        results.check("Cluster keeps the clock", (clock != null) && (clock.confirmed_epoch === EPOCH) &&
                      (clock.confirmed_slot === SLOT) && (clock.confirmed_unix_timestamp === BLOCK_TIME) &&
                      (typeof(clock.slot) == "number") && (typeof(clock.unix_timestamp) == "number"));

        // The authority account
        let authority = await cluster.get_authority();
        results.check("Cluster reads the authority account", (authority != null) &&
                      (authority.mystery_escrow_lamports === 7000000000n) &&
                      (authority.rent_exempt_minimum_lamports === BigInt(RENT_EXEMPT_MINIMUM)) &&
                      (authority.excess_lamports === 1000000000n),
                      to_json(authority));

        // A stake account, with its validator's name
        let stake = await cluster.get_stake_account(STAKE_ACCOUNT);
        results.check("Cluster reads a stake account", (stake != null) &&
                      (stake.delegation.lamports === STAKE_LAMPORTS) &&
                      (stake.delegation.vote_account == Base.g_shinobi_systems_vote_address), to_json(stake));

        // The Ki that the entry can harvest: its 2 SOL of earnings since its last harvest, on its effective stake
        // curve, reduced for its 10 days of life, at its level's ki_factor
        let expected_ki = Base.compute_retained_ki_harvest(
            Base.compute_effective_stake_earnings(2000000000n, STAKE_LAMPORTS), 1000,
            Base.compute_ki_harvest_retention(4294967, 10n));
        let harvestable = await entry.get_harvestable_ki(rpc_connections);
        results.check("Entry computes its harvestable Ki", (harvestable === expected_ki) && (expected_ki > 0n),
                      harvestable + " != " + expected_ki);

        results.check("Entry reads its Metaplex metadata's update authority",
                      (await entry.get_metaplex_metadata_uri(rpc_connections)) != null);


        // Wallet -----------------------------------------------------------------------------------------------------

        let wallet = SDK.Wallet.create(cluster, WALLET);
        results.check("Wallet gives its balance as an exact BigInt",
                      (await wallet.get_sol_balance()) === 9007199254740993n);
        results.check("Wallet gives its Ki in whole Ki, as a BigInt",
                      (await wallet.get_ki_balance()) === 12345678901234567n);
        results.check("Wallet finds its entry (whose token is frozen, as it is staked)",
                      to_json([ ...(await wallet.get_entry_addresses()) ]) == to_json([ ENTRY_ADDRESS ]));
        let bids = [ ...(await wallet.get_bids()) ];
        results.check("Wallet finds its bid", (bids.length == 1) && (bids[0].bid_address == BID_ADDRESS) &&
                      (bids[0].entry_address == Base.get_entry_address(BID_ENTRY_MINT)) &&
                      (bids[0].lamports === 3500000000n), to_json(bids));
        let stakes = new Map([ ...(await wallet.get_stakes()) ].map((s) => [ s.address, s ]));
        results.check("Wallet finds its own stake account, and its staked entry's, but not one with a lockup",
                      (stakes.size == 2) && stakes.has(OWN_STAKE_ACCOUNT) && stakes.has(STAKE_ACCOUNT),
                      to_json([ ...stakes.keys() ]));
        let own = stakes.get(OWN_STAKE_ACCOUNT);
        results.check("Wallet's stake account has its delegation and validator's name", (own != null) &&
                      (own.delegation.lamports === 5000000000n) && (own.delegation.vote_account == VOTE_ACCOUNT) &&
                      (own.delegation.validator_name == "Example Validator"), to_json(own));
        results.check("Wallet's staked entry's stake account is marked as such",
                      (stakes.get(STAKE_ACCOUNT) != null) && (stakes.get(STAKE_ACCOUNT).is_immortal_staked == true));
    }
    finally {
        cluster.shutdown();
    }

    results.finish();
}


main().then(() => process.exit(0)).catch((error) => {
    console.log("FAIL: " + results.name + ": " + error.stack);
    process.exit(1);
});
