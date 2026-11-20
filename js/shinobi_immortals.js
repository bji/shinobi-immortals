'use strict';

const SolanaWeb3 = require("@solana/web3.js");
const Buffer = require("buffer");
const bs58 = require("bs58");

const LAMPORTS_PER_SOL = 1000 * 1000 * 1000;

const BLOCKS_AT_ONCE = 3;
const ENTRIES_AT_ONCE = 20;

// Turns an address into a SolanaWeb3.PublicKey
function make_pubkey(address)
{
    return new SolanaWeb3.PublicKey(address);
}

// Finds a Program Derived Address
function find_pda(seeds, program_id)
{
    return SolanaWeb3.PublicKey.findProgramAddressSync(seeds, program_id);
}

const g_system_program_address = "11111111111111111111111111111111";
const g_system_program_pubkey = make_pubkey(g_system_program_address);

const g_self_program_address = "Shin1cdrR1pmemXZU3yDV3PnQ48SX9UmrtHF4GbKzWG";
const g_self_program_pubkey = make_pubkey(g_self_program_address);

const g_metaplex_program_address = "metaqbxxUerdq28cj1RbAWkYQm3ybzjb6a8bt518x1s";
const g_metaplex_program_pubkey = make_pubkey(g_metaplex_program_address);

const g_spl_token_program_address = "TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA";
const g_spl_token_program_pubkey = make_pubkey(g_spl_token_program_address);

const g_spl_associated_token_program_address = "ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL";
const g_spl_associated_token_program_pubkey = make_pubkey(g_spl_associated_token_program_address);

const g_stake_program_address = "Stake11111111111111111111111111111111111111";
const g_stake_program_pubkey = make_pubkey(g_stake_program_address);

const g_config_program_address = "Config1111111111111111111111111111111111111";
const g_config_program_pubkey = make_pubkey(g_config_program_address);

const g_validator_info_address = "Va1idator1nfo111111111111111111111111111111";
const g_validator_info_pubkey = make_pubkey(g_validator_info_address);

const g_program_authority_pubkey = find_pda([ [ 2 ] ], g_self_program_pubkey)[0];
const g_program_authority_address = g_program_authority_pubkey.toBase58();

const g_program_state_pubkey = find_pda([ [ 1 ] ], g_self_program_pubkey)[0];
const g_program_state_address = g_program_state_pubkey.toBase58();

// The Shinobi Systems vote account, which is compiled into the program (see GNUmakefile).  It is the only validator
// that the program delegates stake accounts to.
const g_shinobi_systems_vote_address = "BLADE1qNA1uNjRgER6DtUFf7FU3c1TWLLdpPeEcKatZ2";

const g_ki_mint_pubkey = find_pda([ [ 3 ] ], g_self_program_pubkey)[0];
const g_ki_mint_address = g_ki_mint_pubkey.toBase58();

const g_ki_metadata_address = get_metaplex_metadata_address(g_ki_mint_address);
const g_ki_metadata_pubkey = make_pubkey(g_ki_metadata_address);

const g_bid_marker_mint_pubkey = find_pda([ [ 7 ] ], g_self_program_pubkey)[0];
const g_bid_marker_mint_address = g_bid_marker_mint_pubkey.toBase58();

const g_clock_sysvar_address = "SysvarC1ock11111111111111111111111111111111";
const g_clock_sysvar_pubkey = make_pubkey(g_clock_sysvar_address);

const g_stake_history_sysvar_address = "SysvarStakeHistory1111111111111111111111111";
const g_stake_history_sysvar_pubkey = make_pubkey(g_stake_history_sysvar_address);

const g_stake_config_address = "StakeConfig11111111111111111111111111111111";
const g_stake_config_pubkey = make_pubkey(g_stake_config_address);

// Instruction codes of the instructions whose transactions are built in this file (see program/entrypoint.c)
const INSTRUCTION_WITHDRAW_EXCESS = 8;
const INSTRUCTION_BUY = 9;
const INSTRUCTION_REFUND = 10;
const INSTRUCTION_BID = 11;
const INSTRUCTION_CLAIM_LOSING = 12;
const INSTRUCTION_STAKE = 13;
const INSTRUCTION_DESTAKE = 14;
const INSTRUCTION_THAW = 15;
const INSTRUCTION_HARVEST = 16;
const INSTRUCTION_LEVEL_UP = 17;
const INSTRUCTION_SETTLE_WINNING_BID = 18;
const INSTRUCTION_DELEGATE = 19;
const INSTRUCTION_UPDATE_EPOCHS_STAKED = 20;

// Data type code of the authority account's data (DataType_Authority in program/inc/data_type.h)
const DATA_TYPE_AUTHORITY = 6;

// The types of account that the admin may withdraw excess lamports from with Wallet.withdraw_excess().  The "code"
// of each is the value that the program knows the type by (WithdrawExcessAccountType in
// program/admin/admin_withdraw_excess.c).
const WithdrawExcessAccountType = Object.freeze(
{
    // The program state account
    ProgramState  : Object.freeze({ code : 0, name : "ProgramState" }),
    // The authority account.  The purchase prices of mysteries that have not yet been revealed or refunded are held
    // there in escrow, and are never withdrawn.
    Authority     : Object.freeze({ code : 1, name : "Authority" }),
    // A block account
    Block         : Object.freeze({ code : 2, name : "Block" }),
    // An entry account
    Entry         : Object.freeze({ code : 3, name : "Entry" }),
    // The whitelist account of a block
    Whitelist     : Object.freeze({ code : 4, name : "Whitelist" })
});


function delay(delay_ms)
{
    return new Promise((resolve) => setTimeout(resolve, delay_ms));
}

function address_to_buffer(address)
{
    return Buffer.Buffer.from(bs58.decode(address));
}


// Single possibly rate-limited RPC connection.  If a rate limit is set, it will observe it, and throw an error
// for any request that would exceed the limit
class RpcConnection
{
    static create(rpc_endpoint, max_requests, max_data)
    {
        return new RpcConnection(rpc_endpoint, max_requests, max_data);
    }

    constructor(rpc_endpoint, max_requests, max_data)
    {
        this.requests_array = [ ];

        this.data_array = [ ];

        this.data_sum = 0;

        this.connection = new SolanaWeb3.Connection(rpc_endpoint, "confirmed");

        this.update(max_requests, max_data);
    }

    update(max_requests, max_data)
    {
        if ((max_requests != null) && ((max_requests.limit == null) || (max_requests.duration == null))) {
            max_requests = null;
        }

        if ((max_data != null) && ((max_data.limit == null) || (max_data.duration == null))) {
            max_data = null;
        }

        this.max_requests = max_requests;

        this.max_data = max_data;
    }

    async getGenesisHash()
    {
        return this.execute(() => { return this.connection.getGenesisHash(); }, 1024);
    }

    async getAccountInfo(address, config)
    {
        let pubkey = make_pubkey(address);
        return this.execute(() => { return this.connection.getAccountInfo(pubkey, config); }, 10 * 1024);
    }

    async getParsedAccountInfo(address, config)
    {
        let pubkey = make_pubkey(address);
        return this.execute(() => { return this.connection.getParsedAccountInfo(pubkey, config); }, 10 * 1024);
    }

    async getEpochInfo()
    {
        return this.execute(() => { return this.connection.getEpochInfo(); }, 1024);
    }

    async getBlockTime(absoluteSlot)
    {
        return this.execute(() => { return this.connection.getBlockTime(absoluteSlot); }, 1024);
    }

    async getMultipleAccountsInfo(block_addresses)
    {
        let block_pubkeys = block_addresses.map((address) => { return make_pubkey(address); });
        return this.execute(() => { return this.connection.getMultipleAccountsInfo(block_pubkeys); },
                            block_pubkeys.length * 10 * 1024);
    }

    async getBalance(wallet_address)
    {
        let wallet_pubkey = make_pubkey(wallet_address);
        return this.execute(() => { return this.connection.getBalance(wallet_pubkey); }, 1024);
    }

    async getParsedTokenAccountsByOwner(wallet_address, program_address)
    {
        let wallet_pubkey = make_pubkey(wallet_address);
        let config;
        if (program_address == null) {
            config = null;
        }
        else {
            config = { programId : make_pubkey(program_address) };
        }
        return this.execute(() => { return this.connection.getParsedTokenAccountsByOwner(wallet_pubkey, config); },
                            20 * 1024);
    }

    async getParsedProgramAccounts(program_address, config)
    {
        let program_pubkey = make_pubkey(program_address);
        return this.execute(() => { return this.connection.getParsedProgramAccounts(program_pubkey, config); },
                            20 * 1024);
    }

    async getProgramAccounts(program_address, config)
    {
        let program_pubkey = make_pubkey(program_address);
        return this.execute(() => { return this.connection.getProgramAccounts(program_pubkey, config); },
                            20 * 1024);
    }

    async getLatestBlockhash()
    {
        return this.execute(() => { return this.connection.getLatestBlockhash(); }, 1024);
    }

    async sendRawTransaction(raw_tx)
    {
        return this.execute(() => { return this.connection.sendRawTransaction(raw_tx); }, 5 * 1024);
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Private implementation details follows -------------------------------------------------------------------------
    // ----------------------------------------------------------------------------------------------------------------

    async execute(f, data_size)
    {
        // Remove already expired request timeouts
        let now = Date.now();
        while (this.requests_array.length > 0) {
            let entry = this.requests_array[0];
            if (entry.until > now) {
                break;
            }
            this.requests_array.shift();
        }

        // Remove already expired data timeouts
        while (this.data_array.length > 0) {
            let entry = this.data_array[0];
            if (entry.until > now) {
                break;
            }
            this.data_sum -= entry.data_size;
            this.data_array.shift();
        }

        // If there is a limit on the maximum number of requests, make sure that limit is not exceeded
        if ((this.max_requests != null) && (this.requests_array.length >= this.max_requests.limit)) {
            throw new Error(this.connection.rpcEndpoint + ": Too many outstanding requests");
        }

        // If there is a limit on the maximum amount of data, make sure that limit is not exceeded
        if ((this.max_data != null) && ((this.data_sum + data_size) > this.max_data.limit)) {
            throw new Error(this.connection.rpcEndpoint + ": Too much request data");
        }

        // Add timeouts for some huge number we call "long enough" -- 10 years
        let future = now + (10 * 365 * 24 * 60 * 60 * 1000);
        let request_until = { until : future };
        let data_until = { until : future, data_size : data_size };

        // Since these are the most recent they definitely go at the end
        this.requests_array.push(request_until);
        this.data_array.push(data_until);
        this.data_sum += data_size;

        try {
            return await(f());
        }
        finally {
            now = Date.now();
            this.finish_request_until(now, request_until);
            this.finish_data_until(now, data_until);
        }
    }

    finish_request_until(now, until)
    {
        // Remove until
        this.requests_array.splice(this.requests_array.indexOf(until), 1);

        // If there is no limit, then there is nothing else to do
        if (this.max_requests == null) {
            return;
        }

        // Add in sorted order a new timeout that is +duration away
        until = { until : now + this.max_requests.duration };

        for (let i = 0; i < this.requests_array.length; i++) {
            if (this.requests_array[i].until > until.until) {
                this.requests_array.splice(i, 0, until);
                return;
            }
        }

        this.requests_array.push(until);
    }

    finish_data_until(now, until)
    {
        // Remove until
        this.data_array.splice(this.data_array.indexOf(until), 1);

        // If there is no limit, then there is nothing else to do, except remove the data_size since it
        // wasn't used
        if (this.max_data == null) {
            this.data_sum -= until.data_size;
            return;
        }

        // Add in sorted order a new timeout that is +duration away
        until = { until : now + this.max_data.duration, data_size : until.data_size };

        for (let i = 0; i < this.data_array.length; i++) {
            if (this.data_array[i].until > until.until) {
                this.data_array.splice(i, 0, until);
                return;
            }
        }

        this.data_array.push(until);
    }
}


// RpcConnections holds a set of endpoints connecting to a single cluster.  It provides functions for executing
// functions against a cluster in a round-robin fashion; and running a function repeatedly on the cluster, until
// stopped.  It also supports rate limits on requests so as not to exceed limits imposed by the RPC server.
class RpcConnections
{
    static create()
    {
        return new RpcConnections();
    }

    // Do not use until the successful completion of the first update() call.
    constructor()
    {
        // Map from rpc_endpoint to { rpc_connection, queued_requests }
        this.map = new Map();

        // List of rpc_connections (not set here since initialized in update)
        // this.array = [ ];

        // Next rpc_connection to use (not set here since initialized in update)
        // this.index = 0;
    }

    // new_rpcpoint_descriptors is either a single string, a single object, or an array of strings and objects.
    // The strings are endpoint URLs with no query limits.  The objects are of this form:
    // {
    //    rpc_endpoint : String,
    //    max_requests : { limit : int, duration (milliseconds) : int },
    //    max_data : { limit : int, duration (bytes) : int },
    // }
    // If max_requests is non-null, it places a maximum of limit requests per duration milliseconds.
    // If max_data is non-null, it places a maximum of limit bytes per duration milliseconds.
    // If rpc_endpoint_descriptors is null, a default is used.  Throws an error if any one of rpc_endpoint_descriptors
    // is not for the same cluster as this RpcConnections instance.
    async update(rpc_endpoint_descriptors)
    {
        // If new_rpc_endpoints is null, then use default set of rpc endpoints
        if (rpc_endpoint_descriptors == null) {
            rpc_endpoint_descriptors =
                [
                    {
                        rpc_endpoint : "https://api.mainnet-beta.solana.com",
                        max_requests : { limit : 40, duration : 10 * 1000 },
                        max_data : { limit : 100 * 1000 * 1000, duration : 30 * 1000 }
                    },
                    "https://ssc-dao.genesysgo.net"
                ];
        }
        else if (!Array.isArray(rpc_endpoint_descriptors)) {
            rpc_endpoint_descriptors = [ rpc_endpoint_descriptors ];
        }

        // Create a new map and array from new_rpc_endpoints (re-using existing RPC connections)
        let new_map = new Map();
        let new_array = [ ];

        for (let descriptor of rpc_endpoint_descriptors) {
            if (typeof(descriptor) == "string") {
                // Not an object, must be a string, so make it into an object
                descriptor = {
                    rpc_endpoint : descriptor,
                    max_requests : null,
                    max_data : null
                };
            }

            let rpc_connection = this.map.get(descriptor.rpc_endpoint);

            if (rpc_connection == null) {
                rpc_connection = new RpcConnection(descriptor.rpc_endpoint,
                                                   descriptor.max_requests,
                                                   descriptor.max_data);

                // Check to make sure that the genesis hash is consistent
                let genesis_hash = await rpc_connection.getGenesisHash();

                if (this.genesis_hash == null) {
                    this.genesis_hash = genesis_hash;
                }
                else if (genesis_hash != this.genesis_hash) {
                    throw new Error("invalid rpc_endpoint");
                }
            }
            else if ((descriptor.max_requests != null) &&
                     (descriptor.max_data != null)) {
                rpc_connection.update(descriptor.max_requests, descriptor.max_data);
            }

            new_array.push(rpc_connection);

            new_map.set(descriptor.rpc_endpoint, rpc_connection);
        }

        this.map = new_map;

        this.array = new_array;

        this.index = 0;
    }

    // Runs the async function, passing it an rpc_connection to use, and returning the result.  If the function throws
    // an error, retries it after 2 seconds, for up to 5 retries.  If the RpcConnections is shut down, throws an
    // error.  If the request cannot be run because the connection is overloaded, throws an error.
    async run(func, error_prefix)
    {
        let rpc_connection = this.get();

        let retry_count = 0;

        while (true) {
            try {
                return await func(this.get());
            }
            catch (error) {
                if (this.is_shutdown) {
                    throw new Error("shutdown");
                }

                if (++retry_count == 11) {
                    throw error;
                }

                await delay(1 * 1000);
            }
        }
    }

    // Runs the async function, passing it an rpc_connection to use, and returning the result.  If the function throws
    // an error or is shut down, throws an error.
    async run_once(func, error_prefix)
    {
        return func(this.get());
    }

    // Runs an async function at fixed intervals until the RpcConnections is shutdown.
    // - When the function throws an error, re-runs it after 1 second
    // - When the function completes successfully, re-runs it after 1 minute
    async loop(func, interval_milliseconds)
    {
        while (true) {
            // Exit the loop when the RpcConnections is shut down
            if (this.is_shutdown) {
                return;
            }

            try {
                await func();

                // On success, re-run the loop after [interval_milliseconds]
                await delay(interval_milliseconds);
            }
            catch (error) {
                // On error, re-run the loop after 1 second
                console.log("Loop error: " + error);

                await delay(1 * 1000);
            }
        }
    }

    // Shuts down the RpcConnections
    shutdown()
    {
        this.is_shutdown = true;
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Private implementation details follows -------------------------------------------------------------------------
    // ----------------------------------------------------------------------------------------------------------------

    get()
    {
        if (this.is_shutdown) {
            throw new Error("shutdown");
        }

        this.index = (this.index + 1) % this.array.length;

        return this.array[this.index];
    }
}


class Cluster
{
    // If default_slot_duration_seconds is null, a default is used.  If slots_per_epoch is null, a default is used.
    static create(rpc_connections, default_slot_duration_seconds, slots_per_epoch, callbacks)
    {
        return new Cluster(rpc_connections, default_slot_duration_seconds, slots_per_epoch, callbacks);
    }

    constructor(rpc_connections, default_slot_duration_seconds, slots_per_epoch, callbacks)
    {
        this.rpc_connections = rpc_connections;

        if (default_slot_duration_seconds == null) {
            if ((rpc_connections.array.length == 1) &&
                (rpc_connections.array[0].rpcEndpoint == "http://localhost:8899")) {
                default_slot_duration_seconds = 0.1;
            }
            else {
                default_slot_duration_seconds = 0.62;
            }
        }

        if (slots_per_epoch == null) {
            if ((rpc_connections.array.length == 1) &&
                (rpc_connections.array[0].rpcEndpoint == "http://localhost:8899")) {
                slots_per_epoch = 2000;
            }
            else {
                slots_per_epoch = 432000;
            }
        }

        this.default_slot_duration_seconds = default_slot_duration_seconds;

        this.slots_per_epoch = slots_per_epoch;

        this.callbacks = callbacks;

        // Map from block address to block
        this.blocks = new Map();

        // Map from entry address to entry
        this.entries = new Map();

        // In-order array of entry addresses
        this.entry_addresses = [ ];

        // Each query for an entry increments an index.  This allows background queries for entries to ignore their
        // results if a refresh occurred since the background query started.
        this.entry_query_index = new Map();

        // Keep track of validator names: { name, timeout }
        this.validator_names = new Map();

        // Loop a clock update on the rpc_connections every 5 seconds
        this.rpc_connections.loop(() => { return this.update_clock(); }, 5 * 1000);

        // Loop an update of all entries on the rpc_connections every 60 seconds
        this.rpc_connections.loop(() => { return this.update_blocks(0, 0); }, 60 * 1000);
    }

    /**
     * Shuts down a cluster
     **/
    shutdown()
    {
        this.is_shutdown = true;

        this.rpc_connections.shutdown();
    }

    /**
     * Gets the cluster default slot_duration_seconds
     **/
    get_default_slot_duration_seconds()
    {
        return this.default_slot_duration_seconds;
    }

    /**
     * Returns { confirmed_epoch, confirmed_slot, confirmed_unix_timestamp, confirmed_epoch_pct_complete,
     *           slot, unix_timestamp, epoch_pct_complete }
     *
     * or null if there is no available clock yet
     *
     * If slot_duration_seconds is provided it is used, otherwise the value provided to the constructor is
     * used.
     *
     * confirmed_epoch is the epoch at the most recently confirmed slot known to the client.
     * confirmed_slot is the most recently confirmed slot known to the client.
     * confirmed_unix_timestamp is the cluster timestamp of the most recently confirmed slot known to the client.
     * epoch_epoch_pct_complete is a floating point number from 0 to 1.
     * slot is an estimation of the current confirmed slot.
     * unix_timestamp is an estimation of the cluster timestamp of slot.
     * epoch_pct_complete is a floating point number from 0 to 1.
     *
     * slot and unix_timestamp MAY GO BACKWARDS in between subsequent calls, beware!
     **/
    get_clock(slot_duration_seconds)
    {
        if (this.confirmed_query_time == null) {
            return null;
        }

        if (slot_duration_seconds == null) {
            slot_duration_seconds = this.default_slot_duration_seconds;
        }

        let now = Date.now();

        let slots_elapsed = ((now - this.confirmed_query_time) / (slot_duration_seconds * 1000)) | 0;

        let slot = this.confirmed_slot + slots_elapsed;

        return {
            confirmed_epoch : this.confirmed_epoch,
            confirmed_slot : this.confirmed_slot,
            confirmed_unix_timestamp : this.confirmed_unix_timestamp,
            confirmed_epoch_pct_complete : (this.confirmed_slot % this.slots_per_epoch) / this.slots_per_epoch,
            slot : slot,
            unix_timestamp : this.confirmed_unix_timestamp + (((now - this.confirmed_query_time) / 1000) | 0),
            epoch_pct_complete : (slot % this.slots_per_epoch) / this.slots_per_epoch
        };
    }

    // Get the entry count
    entry_count()
    {
        return this.entry_addresses.length;
    }

    get_entry(entry_address)
    {
        return this.entries.get(entry_address);
    }

    // Get the entry at a given index
    entry_at(index)
    {
        return this.get_entry(this.entry_addresses[index]);
    }

    // Return an iterator over all entries
    entry_iter()
    {
        let index = 0;
        let stop = this.entry_count();

        return {
            value : () =>
            {
                return this.entry_at(index++);
            },

            done : () =>
            {
                return index == stop;
            }
        };
    }

    /**
     * Gets the Shinobi Systems vote account for this cluster.
     **/
    async get_shinobi_systems_vote_account()
    {
        return g_shinobi_systems_vote_address;
    }

    // Returns the stake details of a stake account.  If account is null, the stake account is looked up; else
    // the account is the parsed RPC response for the stake account and is used.
    async get_stake_account(stake_account_address, account)
    {
        if (account == null) {
            account = await this.rpc_connections.run((rpc_connection) => {
                return rpc_connection.getParsedAccountInfo(stake_account_address);
            });
            if (account != null) {
                account = account.value;
            }
        }

        if (account == null) {
            return null;
        }

        let stake = { address : stake_account_address };

        if ((account.data.parsed.info.stake == null) ||
            (account.data.parsed.info.stake.delegation == null)) {
            stake.lamports = account.lamports;
        }
        else {
            let delegation = account.data.parsed.info.stake.delegation;
            stake.delegation = { lamports : delegation.stake, vote_account : delegation.voter };

            // If the name of the stake account hasn't been fetched in 1 hour, fetch it now
            let validator_name = await this.get_validator_name(delegation.voter);

            if (validator_name != null) {
                stake.delegation.validator_name = validator_name;
            }
        }

        return stake;
    }

    /**
     * Fetches the program's authority account, which is where the purchase prices of mysteries are held in escrow
     * until their entries are revealed or refunded.  Returns null if the account does not exist or is not valid;
     * otherwise returns:
     *
     * {
     *     address : string address of the authority account,
     *     lamports : BigInt, everything that the account holds,
     *     rent_exempt_minimum_lamports : BigInt, the rent exempt minimum of the account,
     *     mystery_escrow_lamports : BigInt, the total purchase price of all mysteries that have been bought and not
     *         yet revealed or refunded,
     *     excess_lamports : BigInt, what the account holds beyond its rent exempt minimum and the escrow; this is
     *         what the admin could withdraw with Wallet.withdraw_excess()
     * }
     **/
    async get_authority()
    {
        let account = await this.rpc_connections.run((rpc_connection) => {
            return rpc_connection.getAccountInfo(g_program_authority_pubkey);
        }, "get authority");

        // The authority account's data is an Authority (program/inc/authority.h): a 4 byte data type, 4 bytes of
        // padding, and the 8 byte escrow total
        if ((account == null) || (account.data == null) || (account.data.length != 16) ||
            (buffer_le_u32(account.data, 0) != DATA_TYPE_AUTHORITY)) {
            return null;
        }

        let rent_exempt_minimum_lamports = await this.get_rent_exempt_minimum(account.data.length);

        let authority = { address : g_program_authority_address,
                          lamports : BigInt(account.lamports),
                          rent_exempt_minimum_lamports : rent_exempt_minimum_lamports,
                          mystery_escrow_lamports : buffer_le_u64(account.data, 8) };

        // The account is required to hold its rent exempt minimum plus the escrow; anything beyond that is excess
        authority.excess_lamports = excess_of(authority.lamports,
                                              rent_exempt_minimum_lamports + authority.mystery_escrow_lamports);

        return authority;
    }

    // Returns as a BigInt the rent exempt minimum, in lamports, of an account with data_length bytes of data
    async get_rent_exempt_minimum(data_length)
    {
        return BigInt(await this.rpc_connections.run((rpc_connection) => {
            return rpc_connection.getMinimumBalanceForRentExemption(data_length);
        }, "get rent exempt minimum"));
    }

    // Causes an entry to be immediately refreshed.  If this causes the Entry to change, asynchronously a callback
    // into the callbacks' on_entry_changed function will be made.
    async refresh_entry(entry)
    {
        let block_result = await this.rpc_connections.run((rpc_connection) => {
            return rpc_connection.getAccountInfo(entry.block.address);
        }, "refresh entry update block");

        let entry_result = await this.rpc_connections.run((rpc_connection) => {
            return rpc_connection.getAccountInfo(entry.address);
        }, "refresh entry update entry");

        let block_updated = entry.block.update(block_result.data);
        let entry_updated = entry.update(entry_result.data);

        if (block_updated || entry_updated) {
            if ((this.callbacks != null) && (this.callbacks.on_entry_changed != null)) {
                setTimeout(() => {
                    if (!this.is_shutdown) {
                        this.callbacks.on_entry_changed(entry);
                    }
                }, 0);
            }
        }
    }

    // Private implementation follows ---------------------------------------------------------------------------------

    async update_clock()
    {
        let epoch_info = await this.rpc_connections.run((rpc_connection) => {
            return rpc_connection.getEpochInfo();
        }, "update_clock get epoch info");

        let block_time = await this.rpc_connections.run((rpc_connection) => {
            return rpc_connection.getBlockTime(epoch_info.absoluteSlot);
        }, "update_clock get block time");

        // Update the current data
        this.confirmed_query_time = Date.now();
        this.confirmed_epoch = epoch_info.epoch;
        this.confirmed_slot = epoch_info.absoluteSlot;
        this.confirmed_unix_timestamp = block_time;
    }

    async update_blocks(group_number, block_number)
    {
        let block_addresses = [ ];

        for (let i = 0; i < BLOCKS_AT_ONCE; i++) {
            block_addresses.push(get_block_address(group_number, block_number + i));
        }

        let results = await this.rpc_connections.run((rpc_connection) =>
            {
                return rpc_connection.getMultipleAccountsInfo(block_addresses);
            }, "update blocks get accounts");

        let entries_promises = [ ];

        let idx;
        for (idx = 0; idx < results.length; idx += 1) {
            if (results[idx] == null) {
                break;
            }
            let block_address = block_addresses[idx];
            let block = this.blocks.get(block_address);
            let block_changed;
            if (block == null) {
                block = new Block(this, block_addresses[idx], results[idx].data);
                // Ignore blocks that are not complete
                if (block.added_entries_count < block.total_entry_count) {
                    continue;
                }
                this.blocks.set(block_address, block);
                block_changed = false;
            }
            else {
                block_changed = block.update(results[idx].data);
            }
            // Initiate an entries query for this block
            entries_promises.push(this.update_entries(block, 0, block_changed));
        }

        // Now await completion of all entry queries
        await Promise.all(entries_promises);

        if (idx == BLOCKS_AT_ONCE) {
            // All blocks were loaded, so immediately try to get more for this group
            return this.update_blocks(group_number, block_number + BLOCKS_AT_ONCE);
        }
        else if ((block_number == 0) && (idx == 0)) {
            // An empty group represents the end of all blocks

            // If there is a callback registered to be told when all entries have been updated, call it
            if ((this.callbacks != null) && (this.callbacks.on_entries_update_complete != null)) {
                setTimeout(() =>  {
                    if (!this.is_shutdown) {
                        this.callbacks.on_entries_update_complete();
                    }
                }, 0);
            }
        }
        else {
            // This group is done, but the next group can immediately be queried
            return this.update_blocks(group_number + 1, 0);
        }
    }

    async update_entries(block, entry_index, block_changed)
    {
        // Compute account keys
        let entry_addresses = [ ];

        for (let i = 0; i < ENTRIES_AT_ONCE; i++) {
            entry_addresses.push(get_entry_address(get_entry_mint_address(block.address, entry_index + i)));
        }

        let results = await this.rpc_connections.run((rpc_connection) =>
            {
                return rpc_connection.getMultipleAccountsInfo(entry_addresses);
            }, "update entries query accounts");

        let idx;
        for (idx = 0; idx < results.length; idx += 1) {
            if (results[idx] == null) {
                break;
            }
            let entry_address = entry_addresses[idx];
            let entry = this.entries.get(entry_address);
            if (entry == null) {
                entry = new Entry(block, entry_addresses[idx], results[idx].data);
                this.entries.set(entry_address, entry);
                this.entry_addresses.push(entry_address);
                if ((this.callbacks != null) && (this.callbacks.on_new_entry != null)) {
                    setTimeout(() => {
                        if (!this.is_shutdown) {
                            this.callbacks.on_new_entry(entry);
                        }
                    }, 0);
                }
            }
            else if (entry.update(results[idx].data) || block_changed) {
                if ((this.callbacks != null) && (this.callbacks.on_entry_changed != null)) {
                    setTimeout(() => {
                        if (!this.is_shutdown) {
                            this.callbacks.on_entry_changed(entry);
                        }
                    }, 0);
                }
            }
        }

        if (idx == ENTRIES_AT_ONCE) {
            // All entries were loaded, so try to get more for this block
            return this.update_entries(block, entry_index + ENTRIES_AT_ONCE, block_changed);
        }
    }

    async get_validator_name(vote_account)
    {
        let validator_name_entry = this.validator_names.get(vote_account);

        let now = Date.now();

        if ((validator_name_entry != null) && ((validator_name_entry.timeout + (60 * 60 * 1000)) >= now)) {
            return validator_name_entry.name;
        }

        // Get the validator account from the vote account
        let result;

        try {
            result = await this.rpc_connections.run((rpc_connection) => {
                return rpc_connection.getAccountInfo(vote_account,
                                                     {
                                                         dataSlice : { offset : 4,
                                                                       length : 32 }
                                                 });
            });
        }
        catch (error) {
            console.log(error);
            result = null;
        }

        if ((result == null) || (result.data == null)) {
            return null;
        }

        let validator_account = new SolanaWeb3.PublicKey(result.data).toBase58();

        try {
            result = await this.rpc_connections.run((rpc_connection) => {
                // Fetch the validator info account for the vote account
                return rpc_connection.getParsedProgramAccounts(g_config_program_address,
                                                               {
                                                                   filters : [
                                                                       {
                                                                           memcmp :
                                                                           {
                                                                               // Config program entry that is storing
                                                                               // validator info
                                                                               offset : 1,
                                                                               bytes : g_validator_info_address
                                                                           }
                                                                       },
                                                                       {
                                                                           memcmp :
                                                                           {
                                                                               // Validator info for the vote account
                                                                               offset : 34,
                                                                               bytes : validator_account
                                                                           }
                                                                       }
                                                                   ]
                                                               });
            });
        }
        catch (error) {
            console.log(error);
            result = null;
        }

        let validator_name;

        if ((result == null) ||
            (result.length != 1) ||
            (result[0].account == null) ||
            (result[0].account.data == null) ||
            (result[0].account.data.parsed == null) ||
            (result[0].account.data.parsed.info == null) ||
            (result[0].account.data.parsed.info.configData == null)) {
            validator_name = null;
        }
        else {
            validator_name = result[0].account.data.parsed.info.configData.name;
        }

        this.validator_names.set(vote_account, { name : validator_name, timeout : now });

        return validator_name;
    }
}


class Block
{
    constructor(entries, address, data)
    {
        this.entries = entries;
        this.address = address;
        this.group_number = buffer_le_u32(data, 8);
        this.block_number = buffer_le_u32(data, 12);
        this.total_entry_count = buffer_le_u16(data, 16);
        this.total_mystery_count = buffer_le_u16(data, 18);
        this.mystery_phase_duration = buffer_le_u32(data, 20);
        this.mystery_start_price_lamports = buffer_le_u64(data, 24);
        this.reveal_period_duration = buffer_le_u32(data, 32);
        this.minimum_price_lamports = buffer_le_u64(data, 40);
        this.has_auction = buffer_le_u32(data, 48);
        this.duration = buffer_le_u32(data, 52);
        this.non_auction_start_price_lamports = buffer_le_u64(data, 56);
        this.whitelist_duration = buffer_le_u32(data, 64);
        this.added_entries_count = buffer_le_u16(data, 72);
        // The time at which the block was completed (0 until then), which is also the beginning of the life span of
        // each of its entries, over which their Ki harvests are reduced
        this.block_start_timestamp = Number(buffer_le_s64(data, 80));
        this.mysteries_sold_count = buffer_le_u16(data, 88);
        this.mystery_phase_end_timestamp = Number(buffer_le_s64(data, 96));
    }

    update(data)
    {
        let new_block = new Block(this.entries, this.address, data);

        // Only check for changes to fields that can change
        let changed = false;

        if (new_block.added_entries_count != this.added_entries_count) {
            this.added_entries_count = new_block.added_entries_count;
            // block_start_timestamp can only change in conjection with added_entries_count
            this.block_start_timestamp = new_block.block_start_timestamp;
            changed = true;
        }

        if (new_block.mysteries_sold_count != this.mysteries_sold_count) {
            this.mysteries_sold_count = new_block.mysteries_sold_count;
            // mystery_phase_end_timestamp can only change in conjection with mysteries_sold_count
            this.mystery_phase_end_timestamp = new_block.mystery_phase_end_timestamp;
            changed = true;
        }

        return changed;
    }

    is_revealable(clock)
    {
        if (this.mysteries_sold_count == this.total_mystery_count) {
            return true;
        }

        return (clock.unix_timestamp > this.get_mystery_deadline());
    }

    // Only returns a meaningful value if the block has not yet reached its reveal criteria
    get_mystery_deadline()
    {
        return this.block_start_timestamp + this.mystery_phase_duration;
    }

    // Mirrors get_mystery_phase_end_timestamp in program/util/util_block.c: if all mysteries were sold, the mystery
    // phase ended when the last one was sold (mystery_phase_end_timestamp); otherwise it ends (or ended) when
    // mystery_phase_duration elapses, and mystery_phase_end_timestamp stays 0.  Only meaningful for a complete block.
    get_mystery_phase_end_timestamp()
    {
        if (this.mysteries_sold_count == this.total_mystery_count) {
            return this.mystery_phase_end_timestamp;
        }

        return this.get_mystery_deadline();
    }
}


// Enumeration of states that an Entry can be in
const EntryState = Object.freeze(
{
    // Entry has not been revealed yet, and is not yet owned
    PreRevealUnowned           : Symbol("PreRevealUnowned"),
    // Entry is owned, but the containing block has not met its reveal criteria yet
    PreRevealOwned             : Symbol("PreRevealOwned"),
    // The block containing the entry has met the reveal criteria, but the entry has not been revealed yet; it's unowned
    WaitingForRevealUnowned    : Symbol("WaitingForRevealUnowned"),
    // The block containing the entry has met the reveal criteria, but the entry has not been revealed yet; it's owned
    WaitingForRevealOwned      : Symbol("WaitingForRevealOwned"),
    // Entry is in auction
    InAuction                  : Symbol("InAuction"),
    // Entry is past its auction and waiting to be claimed
    WaitingToBeClaimed         : Symbol("WaitingToBeClaimed"),
    // Entry is past its auction end period but is not owned
    Unowned                    : Symbol("Unowned"),
    // Entry is owned and revealed, but not staked
    Owned                      : Symbol("Owned"),
    // Entry is owned, revealed, and staked
    OwnedAndStaked             : Symbol("OwnedAndStaked"),
    // Entry's metadata has been reauthorized to another program; the entry is retired and the program will not perform
    // any further operation on it (other than letting losing bidders reclaim their bids)
    Reauthorized               : Symbol("Reauthorized")
});


class Entry
{
    constructor(block, address, data)
    {
        this.block = block;
        this.address = address;
        this.group_number = buffer_le_u32(data, 36);
        this.block_number = buffer_le_u32(data, 40);
        this.entry_index = buffer_le_u16(data, 44);
        this.mint_address = buffer_address(data, 46);
        this.token_address = buffer_address(data, 78);
        this.metaplex_metadata_address = buffer_address(data, 110);
        this.minimum_price_lamports = buffer_le_u64(data, 144);
        this.has_auction = data[152];
        this.duration = buffer_le_u32(data, 156);
        this.non_auction_start_price_lamports = buffer_le_u64(data, 160);
        this.reveal_sha256 = buffer_sha256(data, 168);
        this.reveal_timestamp = Number(buffer_le_s64(data, 200));
        this.purchase_price_lamports = buffer_le_u64(data, 208);
        this.refund_awarded = data[216];
        // Nonzero once the entry's metadata has been reauthorized to another program; the entry is then retired
        this.reauthorized = data[217];
        this.auction_highest_bid_lamports = buffer_le_u64(data, 224);
        this.auction_winning_bid_address = buffer_address(data, 232);
        this.owned_stake_account = buffer_address(data, 264);
        this.owned_last_ki_harvest_stake_account_lamports = buffer_le_u64(data, 296);
        this.level = data[304];
        this.most_recent_ki_applied_window = buffer_le_u64(data, 3160);
        this.ki_applied_this_window = buffer_le_u64(data, 3168);
        this.total_level_credits = buffer_le_u64(data, 3176);
        // The number of whole epochs that the entry has been staked with stake active at Shinobi Systems, as last
        // counted by the program, and the epoch up to which it has been counted (see program/inc/entry.h)
        this.epochs_staked = buffer_le_u64(data, 3184);
        this.epochs_staked_counted_to_epoch = buffer_le_u64(data, 3192);

        this.metadata_level_1_credits = buffer_le_u32(data, 308);
        this.metadata_random = [ buffer_le_u32(data, 312),
                                 buffer_le_u32(data, 316),
                                 buffer_le_u32(data, 320),
                                 buffer_le_u32(data, 324),
                                 buffer_le_u32(data, 328),
                                 buffer_le_u32(data, 332),
                                 buffer_le_u32(data, 336),
                                 buffer_le_u32(data, 340),
                                 buffer_le_u32(data, 344),
                                 buffer_le_u32(data, 348),
                                 buffer_le_u32(data, 352),
                                 buffer_le_u32(data, 356),
                                 buffer_le_u32(data, 360),
                                 buffer_le_u32(data, 364),
                                 buffer_le_u32(data, 368),
                                 buffer_le_u32(data, 372) ];
        this.level_metadata = [ ];

        for (let i = 0; i < 9; i += 1) {
            this.level_metadata[i] = {
                // form is a uint32_t in LevelMetadata (program/inc/entry.h)
                form : buffer_le_u32(data, 376 + (i * 308)),
                skill : data[380 + (i * 308)],
                ki_factor : buffer_le_u32(data, 384 + (i * 308)),
                name : buffer_string(data, 388 + (i * 308), 32),
                title : buffer_string(data, 420 + (i * 308), 32),
                uri : buffer_string(data, 452 + (i * 308), 200),
                uri_contents_sha256 : buffer_sha256(data, 652 + (i * 308))
            };
        }

        // Expected annual stake reward rate in basis points, which calibrates the level credits curve
        this.metadata_expected_annual_reward_bps = buffer_le_u32(data, 3148);

        // The fraction, of UINT32_MAX, by which Ki harvests are reduced for each day of the entry's life span
        this.metadata_ki_harvest_reduction_per_day = buffer_le_u32(data, 3152);
    }

    update(data)
    {
        let new_entry = new Entry(this.block, this.address, data);

        // Only check for changes to fields that can change
        let changed = false;

        if (new_entry.reveal_sha256 != this.reveal_sha256) {
            this.reveal_sha256 = new_entry.reveal_sha256;
            // reveal_timestamp can only change when reveal_sha256 changes
            this.reveal_timestamp = Number(buffer_le_s64(data, 200));
            changed = true;
        }

        if (new_entry.purchase_price_lamports != this.purchase_price_lamports) {
            this.purchase_price_lamports = new_entry.purchase_price_lamports;
            changed = true;
        }

        // A bid placed near the end of an auction extends the auction by increasing duration
        if (new_entry.duration != this.duration) {
            this.duration = new_entry.duration;
            changed = true;
        }

        if (new_entry.refund_awarded != this.refund_awarded) {
            this.refund_awarded = new_entry.refund_awarded;
            changed = true;
        }

        if (new_entry.reauthorized != this.reauthorized) {
            this.reauthorized = new_entry.reauthorized;
            changed = true;
        }

        if (new_entry.auction_highest_bid_lamports != this.auction_highest_bid_lamports) {
            this.auction_highest_bid_lamports = new_entry.auction_highest_bid_lamports;
            changed = true;
        }

        if (new_entry.auction_winning_bid_address != this.auction_winning_bid_address) {
            this.auction_winning_bid_address = new_entry.auction_winning_bid_address;
            changed = true;
        }

        if (new_entry.owned_stake_account != this.owned_stake_account) {
            this.owned_stake_account = new_entry.owned_stake_account;
            changed = true;
        }

        if (new_entry.owned_last_ki_harvest_stake_account_lamports !=
            this.owned_last_ki_harvest_stake_account_lamports) {
            this.owned_last_ki_harvest_stake_account_lamports = new_entry.owned_last_ki_harvest_stake_account_lamports;
            changed = true;
        }

        if (new_entry.level != this.level) {
            this.level = new_entry.level;
            changed = true;
        }

        // Contributions can change progression without changing the level.
        for (const field of [ "most_recent_ki_applied_window", "ki_applied_this_window", "total_level_credits",
                              "epochs_staked", "epochs_staked_counted_to_epoch" ]) {
            if (new_entry[field] != this[field]) {
                this[field] = new_entry[field];
                changed = true;
            }
        }

        // Compare against the level metadata that the constructor parsed for new_entry, so that the byte offsets of the
        // level metadata are only in one place (the constructor)
        let new_level_metadata = new_entry.level_metadata;

        let level_metadata_changed = false;

        for (let i = 0; i < 9; i += 1) {
            if ((new_level_metadata[i].form != this.level_metadata[i].form) ||
                (new_level_metadata[i].skill != this.level_metadata[i].skill) ||
                (new_level_metadata[i].ki_factor != this.level_metadata[i].ki_factor) ||
                (new_level_metadata[i].name != this.level_metadata[i].name) ||
                (new_level_metadata[i].title != this.level_metadata[i].title) ||
                (new_level_metadata[i].uri != this.level_metadata[i].uri) ||
                (new_level_metadata[i].uri_contents_sha256 != this.level_metadata[i].uri_contents_sha256)) {
                level_metadata_changed = true;
            }
        }

        if (level_metadata_changed) {
            this.level_metadata = new_level_metadata;
            changed = true;
        }

        return changed;
    }

    // Returns the metaplex metadata URI for the entry, as stored in the metaplex metadata of the entry
    async get_metaplex_metadata_uri(rpc_connections)
    {
        let result = await rpc_connections.run((rpc_connection) => {
            return rpc_connection.getAccountInfo(this.metaplex_metadata_address);
        }, "fetch metaplex metadata");

        let data = result.data;

        // Skip key, update_authority, mint
        let offset = 1 + 32 + 32;
        // name_len
        let len = buffer_le_u32(data, offset);
        if (len > 200) {
            // Bogus data
            throw new Error("Invalid metaplex metadata for " + this.metaplex_metadata_address);
        }
        // Skip name
        offset += 4 + len;
        // Symbol, string of at most 4 characters
        len = buffer_le_u32(data, offset);
        if (len > 10) {
            throw new Error("Invalid metaplex metadata for " + this.metaplex_metadata_address);
        }
        // Skip symbol;
        offset += 4 + len;
        // Uri, string of at most 200 characters
        len = buffer_le_u32(data, offset);
        if (len > 200) {
            throw new Error("Invalid metaplex metadata for " + this.metaplex_metadata_address);
        }

        return buffer_string(data, offset + 4, len);
    }

    // Cribbed from the program's get_entry_state() function
    get_entry_state(clock)
    {
        // A reauthorized entry is retired, whatever its other fields say
        if (this.reauthorized) {
            return EntryState.Reauthorized;
        }

        if (this.reveal_sha256 == "0000000000000000000000000000000000000000000000000000000000000000") {
            if (this.purchase_price_lamports > 0) {
                if (this.owned_stake_account == g_system_program_address) {
                    return EntryState.Owned;
                }
                else {
                    return EntryState.OwnedAndStaked;
                }
            }
            else {
                if (this.has_auction) {
                    // The auction is over once the cluster clock (not the local clock) reaches its end
                    if ((this.reveal_timestamp + this.duration) > clock.unix_timestamp) {
                        return EntryState.InAuction;
                    }
                    else if (this.auction_highest_bid_lamports > 0) {
                        return EntryState.WaitingToBeClaimed;
                    }
                }
                return EntryState.Unowned;
            }
        }
        else if (this.block.is_revealable(clock)) {
            if (this.purchase_price_lamports > 0) {
                return EntryState.WaitingForRevealOwned;
            }
            else {
                return EntryState.WaitingForRevealUnowned;
            }
        }
        else if (this.purchase_price_lamports) {
            return EntryState.PreRevealOwned;
        }
        else {
            return EntryState.PreRevealUnowned;
        }
    }

    // This only returns a valid value if the entry state is PreRevealUnowned or Unowned
    get_price(clock)
    {
        if (this.get_entry_state(clock) == EntryState.PreRevealUnowned) {
            return Entry.compute_price(BigInt(this.block.mystery_phase_duration),
                                       this.block.mystery_start_price_lamports,
                                       this.block.minimum_price_lamports,
                                       Entry.seconds_since(clock, this.block.block_start_timestamp));
        }
        else if (this.has_auction) {
            return this.block.minimum_price_lamports;
        }
        else {
            return Entry.compute_price(BigInt(this.duration),
                                       this.non_auction_start_price_lamports,
                                       this.minimum_price_lamports,
                                       Entry.seconds_since(clock, this.reveal_timestamp));
        }
    }

    // This only returns a valid value if the entry state is InAuction
    get_auction_minimum_bid_lamports(clock)
    {
        return Entry.compute_minimum_bid(this.minimum_price_lamports, this.auction_highest_bid_lamports);
    }

    // This only returns a valid value if the entry state is InAuction.  A bid placed near the end of an auction extends
    // it by increasing duration, so this can move later after each such bid.
    get_auction_end_unix_timestamp(clock)
    {
        return this.reveal_timestamp + this.duration;
    }

    // This only returns a valid value if the entry state is WaitingForRevealOwned
    get_reveal_deadline()
    {
        return (this.block.get_mystery_phase_end_timestamp() + this.block.reveal_period_duration);
    }

    // Returns, as a BigInt number of whole Ki, the amount of Ki that a Harvest of this entry would mint now.  This
    // mirrors harvest_ki in program/util/util_ki.c.
    async get_harvestable_ki(rpc_connections)
    {
        // If it's not revealed yet, it couldn't be staked
        if ((this.reveal_sha256 != "0000000000000000000000000000000000000000000000000000000000000000") ||
            // If it's not purchased yet, it couldn't be staked
            (this.purchase_price_lamports == 0) ||
            // If it has no stake account, then it's not staked
            (this.owned_stake_account == g_system_program_address)) {
            return 0n;
        }

        // Fetch the stake account data up to the end of the delegation's deactivation_epoch.  The stake account
        // layout is: state (u32) at 0, then Meta (rent_exempt_reserve, authorities, lockup) to 124, then the
        // delegation: voter_pubkey at 124, stake at 156, activation_epoch at 164, deactivation_epoch at 172.
        let result = await rpc_connections.run((rpc_connection) => {
            return rpc_connection.getAccountInfo(this.owned_stake_account,
                                                 {
                                                     dataSlice : { offset : 0,
                                                                   length : 180 }
                                                 });
        }, "fetch stake account");

        if ((result == null) || (result.data == null) || (result.data.length < 180)) {
            return 0n;
        }

        // Only a stake account in the Stake state (2) and delegated to Shinobi Systems earns Ki; whether it is
        // deactivating or deactivated does not matter
        if ((buffer_le_u32(result.data, 0) != 2) ||
            (buffer_address(result.data, 124) != g_shinobi_systems_vote_address)) {
            return 0n;
        }

        let stake = buffer_le_u64(result.data, 156);
        let activation_epoch = buffer_le_u64(result.data, 164);

        // A stake account that is still activating has not earned anything yet
        let epoch_info = await rpc_connections.run((rpc_connection) => {
            return rpc_connection.getEpochInfo();
        }, "fetch epoch info");

        if ((epoch_info == null) || (activation_epoch >= BigInt(epoch_info.epoch))) {
            return 0n;
        }

        // A decrease in delegated stake is not earnings (the program resets its baseline in that case)
        if (stake < this.owned_last_ki_harvest_stake_account_lamports) {
            return 0n;
        }

        // Apply the effective stake curve to what was earned
        let effective_earnings = Entry.compute_effective_stake_earnings(
            stake - this.owned_last_ki_harvest_stake_account_lamports, stake);

        // Reduce it to the fraction that the entry retains at the current point of its life span, which begins when
        // its block was completed, and is measured in whole days.  The cluster's current time is the block time of the
        // slot that the epoch info was fetched at, just as update_clock obtains it.
        let block_time = await rpc_connections.run((rpc_connection) => {
            return rpc_connection.getBlockTime(epoch_info.absoluteSlot);
        }, "fetch block time");
        if (block_time == null) {
            return 0n;
        }
        let lifespan_seconds = BigInt(block_time) - BigInt(this.block.block_start_timestamp);
        let lifespan_days = (lifespan_seconds > 0n) ? (lifespan_seconds / 86400n) : 0n;
        let retention = Entry.compute_ki_harvest_retention(this.metadata_ki_harvest_reduction_per_day, lifespan_days);
        let retained_earnings = (effective_earnings * retention) / 4294967295n;

        // Then convert to whole Ki at the current level's ki_factor
        return (retained_earnings * BigInt(this.level_metadata[this.level].ki_factor)) / BigInt(LAMPORTS_PER_SOL);
    }

    // Private implementation follows ---------------------------------------------------------------------------------

    // Returns as a BigInt the number of seconds from timestamp to the clock's unix_timestamp, or 0 if the clock is
    // earlier than timestamp (the client's estimate of the cluster clock can lag); a negative elapsed time would
    // otherwise produce a wrong price from compute_price.
    static seconds_since(clock, timestamp)
    {
        let elapsed = clock.unix_timestamp - timestamp;

        return BigInt((elapsed > 0) ? elapsed : 0);
    }

    // Mirrors compute_ki_harvest_retention in program/util/util_calculations.c: returns as a BigInt the fraction, of
    // UINT32_MAX, of a Ki harvest that is retained by an entry whose life span is lifespan_days whole days (a BigInt)
    // and whose harvests are reduced by reduction_per_day (a fraction of UINT32_MAX) per day.  It is computed by
    // repeated squaring, rounding down each product to a whole fraction of UINT32_MAX exactly as the program does, so
    // that the result is identical.
    static compute_ki_harvest_retention(reduction_per_day, lifespan_days)
    {
        const ONE = 4294967295n;
        let base = ONE - BigInt(reduction_per_day);
        let result = ONE;
        while (lifespan_days > 0n) {
            if (lifespan_days & 1n) {
                result = (result * base) / ONE;
            }
            if (result == 0n) {
                break;
            }
            base = (base * base) / ONE;
            lifespan_days >>= 1n;
        }
        return result;
    }

    // Mirrors compute_effective_stake_earnings in program/util/util_calculations.c, in BigInt lamports.  With
    // K = EFFECTIVE_STAKE_CEILING_SOL (50 SOL) and x the stake, earnings are scaled by K(K + x) / (K(K + x) + x^2).
    // The program computes x and K in units of 100 lamports, made ten times larger for as long as x is more than
    // 2,000,000,000 units, and computes the floor of the scaled earnings exactly; this does the same.
    static compute_effective_stake_earnings(earnings_lamports, stake_lamports)
    {
        let stake_units = stake_lamports / 100n;
        let ceiling_units = 50n * (1000000000n / 100n);
        while (stake_units > 2000000000n) {
            stake_units /= 10n;
            ceiling_units /= 10n;
        }

        let numerator = ceiling_units * (ceiling_units + stake_units);
        let denominator = numerator + (stake_units * stake_units);

        return (earnings_lamports * numerator) / denominator;
    }

    // Mirrors compute_price in program/util/util_calculations.c, in BigInt lamports.  With T = total_seconds,
    // t = seconds_elapsed and k = PRICE_CURVE_STEEPNESS (1069), before the end of the sale the price is
    //   end_price + floor(floor((start_price - end_price) * T / (kt + T)) * k(T - t) / ((k + 1)T)),
    // which is the curve 1/(kx + 1) - 1/(k + 1), x = t/T, scaled between the start and end prices: it falls to 1/10
    // of the way from end_price to start_price after 1/120 of the sale (2 hours of a 10 day sale), then keeps falling
    // more and more slowly to end_price at the end.  BigInt division of non-negative values rounds down, as the
    // program's does.
    static compute_price(total_seconds, start_price, end_price, seconds_elapsed)
    {
        const PRICE_CURVE_STEEPNESS = 1069n;

        if (seconds_elapsed >= total_seconds) {
            return end_price;
        }

        let added = ((start_price - end_price) * total_seconds) /
            ((PRICE_CURVE_STEEPNESS * seconds_elapsed) + total_seconds);

        added = (added * PRICE_CURVE_STEEPNESS * (total_seconds - seconds_elapsed)) /
            ((PRICE_CURVE_STEEPNESS + 1n) * total_seconds);

        return end_price + added;
    }

    // Mirrors compute_minimum_bid in program/util/util_calculations.c: the first bid must be at least the initial
    // minimum; every later bid must exceed the current highest bid by at least MINIMUM_BID_INCREMENT_BPS of it,
    // rounded up and at least 1 lamport, saturating at the largest u64 value.  Returns 0 if no bid is possible.
    static compute_minimum_bid(initial_minimum_bid, current_max_bid)
    {
        const U64_MAX = 18446744073709551615n;
        // MINIMUM_BID_INCREMENT_BPS: 500 basis points, which is 5%
        const MINIMUM_BID_INCREMENT_BPS = 500n;

        if (current_max_bid == U64_MAX) {
            return 0n;
        }

        if (current_max_bid < initial_minimum_bid) {
            return initial_minimum_bid;
        }

        let increment = ((current_max_bid * MINIMUM_BID_INCREMENT_BPS) + 9999n) / 10000n;
        if (increment == 0n) {
            increment = 1n;
        }

        let result = current_max_bid + increment;

        return (result > U64_MAX) ? U64_MAX : result;
    }
}


// View of a Cluster that supplies information about the contents of the wallet, and provides wallet-specific actions
// All addresss passed into and returned from Wallet are strings.
class Wallet
{
    static create(cluster, wallet_address, callbacks)
    {
        return new Wallet(cluster, wallet_address, callbacks);
    }

    // wallet_address is a base-58 string
    constructor(cluster, wallet_address, callbacks)
    {
        this.cluster = cluster;

        this.wallet_address_holder = { wallet_address : null };

        this.set_wallet_address(wallet_address);

        // Fetch SOL balance only twice every 5 seconds
        // this.last_balance_fetch_time = 0;
        // this.sol_balance = null;

        // Fetch wallet tokens only once every 30 seconds
        // this.last_tokens_fetch_time = 0;

        // Fetch wallet stakes only once every 30 seconds
        // this.last_stakes_fetch_time = 0;
    }

    set_wallet_address(new_wallet_address)
    {
        if (new_wallet_address == null) {
            if (this.wallet_address_holder.wallet_address == null) {
                return;
            }
        }
        else {
            if ((this.wallet_address_holder.wallet_address != null) &&
                (this.wallet_address_holder.wallet_address == new_wallet_address)) {
                return;
            }
        }

        this.wallet_address_holder = { wallet_address : new_wallet_address };

        // Reset saved state because the wallet address has changed
        this.last_balance_fetch_time = null;
        this.sol_balance = null;
        this.ki_balance = null;
        this.entry_addresses = new Set();
        this.bids_by_entry_address = new Map();
        this.bids_by_entry_mint = new Map();
        this.stakes_by_address = new Map();
        this.last_tokens_fetch_time = null;
        this.last_stakes_fetch_time = null;
    }

    // Returns a string, or null
    get_wallet_address()
    {
        return (this.wallet_address_holder == null) ? null : this.wallet_address_holder.wallet_address;
    }

    async get_sol_balance()
    {
        // Save wallet_address_holder so that we know if it changed
        let wallet_address_holder = this.wallet_address_holder;
        let wallet_address = this.wallet_address_holder.wallet_address;

        // If the wallet has been "shut down", don't query
        if (wallet_address == null) {
            return null;
        }

        let now = Date.now();

        // If never fetched balance, fetch it
        if ((this.last_balance_fetch_time == null) ||
            // If haven't fetched balance for 5 seconds, fetch it
            ((now - this.last_balance_fetch_time) > 5000)) {

            // Wait on getting SOL balance of wallet
            let sol_balance = await this.cluster.rpc_connections.run((rpc_connection) => {
                return rpc_connection.getBalance(wallet_address);
            }, "update sol balance");

            if (wallet_address_holder == this.wallet_address_holder) {
                this.sol_balance = sol_balance;
                this.last_balance_fetch_time = Date.now();
            }
        }

        return this.sol_balance;
    }

    async get_ki_balance()
    {
        await this.update_token_data();

        return this.ki_balance;
    }

    // Returns the addresses of entries owned by this
    async get_entry_addresses()
    {
        await this.update_token_data();

        return this.entry_addresses.values();
    }

    // Returns an iterator over { entry_address, bid_address, lamports }.  lamports is the number of lamports in the
    // bid account, which could be more than the bid if lamports were added post-bid.
    async get_bids()
    {
        await this.update_token_data();

        return this.bids_by_entry_address.values();
    }

    // Returns an interator over { address : string,
    //                             lamports : int,
    //                             delegation : null or { lamports : int,
    //                                                    vote_account : string,
    //                                                    validator_name : string or null },
    //                             is_immortal_staked : true or null
    //                           }
    async get_stakes()
    {
        await this.update_stakes();

        return this.stakes_by_address.values();
    }

    // Returns null if the wallet is not withdraw authority for the stake account; otherwise returns the stake account
    // details
    async get_stake(stake_address)
    {
        await this.update_stakes();

        return this.stakes_by_address.get(stake_address);
    }

    // Returns true if the wallet owns the entry with the given string entry address, false if not
    async owns_entry(entry_address)
    {
        await this.update_token_data();

        return this.entry_addresses.has(entry_address);
    }

    // Returns undefined if the wallet has no bid for the string entry address, or the lamports of the bid if it does
    async get_bid(entry_address)
    {
        await this.update_token_data();

        return this.bids_by_entry_address.get(entry_address);
    }

    async update_token_data()
    {
        // Save wallet_address_holder so that we know if it changed
        let wallet_address_holder = this.wallet_address_holder;
        let wallet_address = this.wallet_address_holder.wallet_address;

        // If the wallet has been "shut down", don't query
        if (wallet_address == null) {
            return null;
        }

        let now = Date.now();

        // If never fetched tokens, fetch them
        if ((this.last_tokens_fetch_time == null) ||
            // If haven't fetched tokens in 30 seconds, fetch them
            ((now - this.last_tokens_fetch_time) > 30000)) {
            let results = await this.cluster.rpc_connections.run((rpc_connection) =>
                {
                    return rpc_connection.getParsedTokenAccountsByOwner(wallet_address, g_spl_token_program_address);
                }, "update token data token accounts");

            let new_ki_balance = 0;

            let new_entry_addresses = new Set();

            let new_bids_by_entry_address = new Map();

            let new_bids_by_entry_mint = new Map();

            let promises = [ ];

            for (let idx = 0; idx < results.value.length; idx++) {
                let account = results.value[idx].account;
                let address = results.value[idx].pubkey.toBase58();

                if ((account == null) || (account.data == null) || (account.data.parsed == null) ||
                    (account.data.parsed.info == null)) {
                    continue;
                }

                // A token account holding a staked entry's token is frozen for as long as the entry is staked (and an
                // entry's token account can also be left frozen by a Destake with no_thaw, until it is thawed with
                // Thaw), so frozen token accounts are included too; otherwise staked entries, and with them their
                // stake accounts (see update_stakes), would vanish from the wallet.  Only uninitialized accounts are
                // skipped.
                if ((account.data.parsed.info.state != "initialized") &&
                    (account.data.parsed.info.state != "frozen")) {
                    continue;
                }

                if (account.data.parsed.info.tokenAmount.amount == 0) {
                    continue;
                }

                // Should never happen but just in case
                if (account.data.parsed.info.owner != wallet_address) {
                    continue;
                }

                let mint_address = account.data.parsed.info.mint;

                // If its mint is the bid mint, then it's a bid token, so add an async function to fetch the
                // details of the bid and add it to new_bids_by_entry_address
                if (mint_address == g_bid_marker_mint_address) {
                    promises.push(this.process_bid(address, new_bids_by_entry_address, new_bids_by_entry_mint));
                }
                // Else if its mint is the ki mint, then add its ki tokens to the total
                else if (mint_address == g_ki_mint_address) {
                    // Ki tokens on chain have 1 decimal place, but in UI representations, they are shown without the
                    // decimal
                    new_ki_balance += ((account.data.parsed.info.tokenAmount.amount / 10) | 0);
                }
                else {
                    // Add an async function to check to see if its update authority is the Shinobi Immortals program
                    // authority, and if so, add it to new_entries.
                    promises.push(this.process_mint(mint_address, new_entry_addresses));
                }
            }

            await Promise.all(promises);

            // Now rebuild this.ki_balance, this.entry_addresses, this.bids_by_entry_address, and
            // this.bids_by_entry_mint from the resulting data
            if (wallet_address_holder == this.wallet_address_holder) {
                this.ki_balance = new_ki_balance;
                this.entry_addresses = new_entry_addresses;
                this.bids_by_entry_address = new_bids_by_entry_address;
                this.bids_by_entry_mint = new_bids_by_entry_mint;

                this.last_tokens_fetch_time = Date.now();
            }
        }
    }

    async update_stakes()
    {
        // Save wallet_address_holder so that we know if it changed
        let wallet_address_holder = this.wallet_address_holder;
        let wallet_address = this.wallet_address_holder.wallet_address;

        // If the wallet has been "shut down", don't query
        if (wallet_address == null) {
            return null;
        }

        let wallet_pubkey = make_pubkey(wallet_address);

        let now = Date.now();

        // If never fetched stakes, fetch them
        if ((this.last_stakes_fetch_time == null) ||
            // If haven't fetched stakes in 30 seconds, fetch them
            ((now - this.last_stakes_fetch_time) > 30000)) {
            let results = await this.cluster.rpc_connections.run((rpc_connection) =>
                {
                    return rpc_connection.getParsedProgramAccounts(g_stake_program_address,
                                                                   {
                                                                       filters : [
                                                                           {
                                                                               memcmp :
                                                                               {
                                                                                   offset : 44,
                                                                                   bytes : wallet_pubkey.toBase58()
                                                                               }
                                                                           }
                                                                       ]
                                                                   });
                }, "update token data token accounts");

            if (wallet_address_holder != this.wallet_address_holder) {
                return;
            }

            // Build the new set of stake accounts separately, and only replace this.stakes_by_address once it is
            // complete, so that callers never see a partially built set
            let new_stakes_by_address = new Map();

            for (let idx = 0; idx < results.length; idx++) {
                let account = results[idx].account;
                let pubkey = results[idx].pubkey;

                if ((account == null) || (account.data == null) || (account.data.parsed == null) ||
                    (account.data.parsed.info == null)) {
                    continue;
                }

                // Ignore stake accounts that are not initialized or delegated
                if ((account.data.parsed.type != "initialized") &&
                    (account.data.parsed.type != "delegated")) {
                    continue;
                }

                // Ignore stake accounts with lockup
                if (account.data.parsed.info.lockup &&
                    ((account.data.parsed.info.lockup.epoch != 0) ||
                     (account.data.parsed.info.lockup.unixTimestamp != 0))) {
                    continue;
                }

                let withdrawer = account.data.parsed.info.meta.authorized.withdrawer;

                if (withdrawer != wallet_address) {
                    continue;
                }

                let stake = await this.cluster.get_stake_account(pubkey.toBase58(), account);

                if (stake != null) {
                    new_stakes_by_address.set(stake.address, stake);
                }
            }

            // Also add in the stake accounts of all entries
            for (let entry_address of await this.get_entry_addresses()) {
                let entry = this.cluster.get_entry(entry_address);
                if ((entry == null) || (entry.owned_stake_account == g_system_program_address)) {
                    continue;
                }
                // Fetch the stake account
                let stake = await this.cluster.get_stake_account(entry.owned_stake_account);
                if (stake != null) {
                    stake.is_immortal_staked = true;
                    new_stakes_by_address.set(stake.address, stake);
                }
            }

            // Only save the results if the wallet address did not change while they were being fetched
            if (wallet_address_holder == this.wallet_address_holder) {
                this.stakes_by_address = new_stakes_by_address;

                this.last_stakes_fetch_time = Date.now();
            }
        }
    }

    async process_bid(bid_marker_token_address, bids_by_entry_address, bids_by_entry_mint)
    {
        let bid_address = get_bid_address(bid_marker_token_address);

        // Read the mint out of the bid account
        let result = await this.cluster.rpc_connections.run((rpc_connection) =>
            {
                return rpc_connection.getAccountInfo(bid_address,
                                                     {
                                                         dataSlice : { offset : 4,
                                                                       length : 32 }
                                                     });
            }, "fetch bid account");

        let entry_mint_address = buffer_address(result.data, 0);

        let entry_address = get_entry_address(entry_mint_address);

        let value = { entry_address : entry_address, bid_address : bid_address, lamports : result.lamports };

        bids_by_entry_address.set(entry_address, value);

        bids_by_entry_mint.set(entry_mint_address, value);
    }

    async process_mint(mint_address, new_entry_addresses)
    {
        // Load the metaplex metadata
        let result = await this.cluster.rpc_connections.run((rpc_connection) => {
            return rpc_connection.getAccountInfo(get_metaplex_metadata_address(mint_address));
        }, "fetch metaplex metadata");

        if (result == null) {
            // No metadata at all
            return;
        }

        let update_authority = buffer_address(result.data, 1);

        if (update_authority == g_program_authority_address) {
            new_entry_addresses.add(get_entry_address(mint_address));
        }
    }

    async fetch_admin_address()
    {
        let result = await this.cluster.rpc_connections.run((rpc_connection) =>
            {
                return rpc_connection.getAccountInfo(g_program_state_address,
                                                     {
                                                         dataSlice : { offset : 4,
                                                                       length : 32 }
                                                     });
            }, "fetch admin address");

        if (result == null) {
            return null;
        }

        return result.data;
    }

    // For all of the following, sign_callback must be an async function with this signature:
    //    async sign_callback(tx_base64_string, slots_until_expiry);
    // where:
    //    tx_base64_string -- is a base64 encoded transaction ready to be offline-signed
    //    slots_until_expiry -- is the number of slots until the tx expires and must be re-submitted
    //
    // The return value of sign_callback must be one of:
    // a. a base64 encoded string version of signed tx
    // b. 0 -- to cancel the transaction
    // c. 1 -- to re-try with a new recent blockhash (i.e. a new expiration_slot); which call back into sign_tx again
    //         with an updated version of the transaction

    // Returns the string transaction id of the completed transaction.  Throws an error on all failures.
    async buy_entry(entry, maximum_price_lamports, sign_callback)
    {
        return this.complete_tx((wallet_address) => {
            return this.make_buy_tx(entry, maximum_price_lamports, wallet_address);
        }, sign_callback);
    }

    async refund_entry(entry, sign_callback)
    {
        return this.complete_tx((wallet_address) => {
            return this.make_refund_tx(entry, wallet_address);
        }, sign_callback);
    }

    async bid_on_entry(entry, minimum_bid_lamports, maximum_bid_lamports, sign_callback)
    {
        return this.complete_tx((wallet_address) => {
            return this.make_bid_tx(entry, minimum_bid_lamports, maximum_bid_lamports, wallet_address);
        }, sign_callback);
    }

    async claim_entry(entry, won, sign_callback)
    {
        return this.complete_tx((wallet_address) => {
            return this.make_claim_tx(entry, won, wallet_address);
        }, sign_callback);
    }

    // Stakes the entry, which the wallet must own, to stake_account, whose withdraw authority must be the wallet.  The
    // stake account is delegated to Shinobi Systems (the only vote account that Stake accepts), and the token account
    // holding the entry's token is frozen until the entry is destaked.
    async stake_entry(entry, stake_account, sign_callback)
    {
        return this.complete_tx((wallet_address) => {
            return this.make_stake_tx(entry, stake_account, wallet_address);
        }, sign_callback);
    }

    // Destake normally harvests the Ki that the entry has earned before it returns the stake account.  If
    // no_ki_harvest is true, then no Ki is harvested, and any Ki that the entry had earned and not yet harvested is
    // forfeited.  This exists so that the stake account can still be recovered if harvesting Ki were ever to fail;
    // there is no other reason to use it, and it should normally be omitted.
    //
    // Destake also normally thaws the entry's token account, which was frozen when the entry was staked.  If no_thaw
    // is true, then the token account is left frozen, and must be thawed with thaw_entry() before the token can be
    // transferred or the entry staked again.  This exists so that the stake account can still be recovered if
    // thawing the token account were ever to fail; there is no other reason to use it, and it should normally be
    // omitted.
    async destake_entry(entry, sign_callback, no_ki_harvest, no_thaw)
    {
        return this.complete_tx((wallet_address) => {
            return this.make_destake_tx(entry, wallet_address, no_ki_harvest, no_thaw);
        }, sign_callback);
    }

    // Thaws the entry's token account, held by the wallet, when it was left frozen by a destake with no_thaw.  This
    // only succeeds if the entry is not staked and its token account is frozen.
    async thaw_entry(entry, sign_callback)
    {
        return this.complete_tx((wallet_address) => {
            return this.make_thaw_tx(entry, wallet_address);
        }, sign_callback);
    }

    async harvest_entry(entry, sign_callback)
    {
        return this.complete_tx((wallet_address) => {
            return this.make_harvest_tx(entry, wallet_address);
        }, sign_callback);
    }

    // ki_to_apply is whole Ki; use a decimal string for large integer amounts.
    async level_up_entry(entry, ki_to_apply, sign_callback)
    {
        return this.complete_tx((wallet_address) => {
            return this.make_level_up_tx(entry, ki_to_apply, wallet_address);
        }, sign_callback);
    }

    // Settles the winning bid of an auction that has ended, on behalf of its bidder: the entry's token goes to the
    // associated token account of bidder_address, the winning bid goes to the admin, and the entry becomes owned.
    // Any wallet may do this, but only once 30 days have passed since the end of the auction; until then only the
    // winner may settle, which the winner does with claim_entry(entry, true, sign_callback).  The wallet pays the
    // transaction fee, and for the creation of the bidder's associated token account if it does not exist yet.
    // bidder_address must be the address of the bidder who won the auction.
    async settle_winning_bid(entry, bidder_address, sign_callback)
    {
        let admin_address = await this.fetch_admin_address();

        if (admin_address == null) {
            throw new Error("Admin address could not be determined");
        }

        return this.complete_tx((wallet_address) => {
            return this.make_settle_winning_bid_tx(entry, bidder_address, admin_address, wallet_address);
        }, sign_callback);
    }

    // Delegates the stake account of a staked entry to Shinobi Systems.  This succeeds only if the stake account is
    // not actively delegated, which is the case when it was staked to the entry while undelegated or delegated to
    // another validator (Stake deactivates it in the latter case; the delegation then succeeds once it has cooled
    // down).  Any wallet may do this; the wallet only pays the transaction fee.
    async delegate(entry, sign_callback)
    {
        return this.complete_tx((wallet_address) => {
            return this.make_delegate_tx(entry, wallet_address);
        }, sign_callback);
    }

    // Brings the entry's epochs_staked up to date, so that it is exact as of the current epoch.  If the entry is not
    // staked, this does nothing, since its epochs_staked is already exact.  Any wallet may do this; the wallet only
    // pays the transaction fee.  A game that needs an exact epochs_staked would normally not use this, but would
    // instead include the instruction returned by make_update_epochs_staked_instruction in its own transaction,
    // before its own instruction.
    async update_epochs_staked(entry, sign_callback)
    {
        return this.complete_tx((wallet_address) => {
            return this.make_update_epochs_staked_tx(entry, wallet_address);
        }, sign_callback);
    }

    // Withdraws to the wallet the excess lamports of an account that the program controls: everything that the
    // account holds beyond what it is required to hold.  The program computes the amount, and fails the transaction
    // if there is no excess.  This only succeeds if the wallet is the admin.
    //
    // account_type is one of the WithdrawExcessAccountType values.  account identifies the account to withdraw from:
    // - for ProgramState and Authority, it is ignored
    // - for Block and Whitelist, it is the Block (or the string address of the block account)
    // - for Entry, it is the Entry (or the string address of the entry account)
    async withdraw_excess(account_type, account, sign_callback)
    {
        return this.complete_tx((wallet_address) => {
            return this.make_withdraw_excess_tx(account_type, account, wallet_address);
        }, sign_callback);
    }

    // Private implementation follows ---------------------------------------------------------------------------------

    // Accounts and data here must match those of scripts/user_buy_tx.sh
    async make_buy_tx(entry, maximum_price_lamports, wallet_address)
    {
        let admin_address = await this.fetch_admin_address();

        if (admin_address == null) {
            throw new Error("Admin address could not be determined");
        }

        let keys = [
            // The funding account, which pays the purchase price
            { pubkey : make_pubkey(wallet_address), isSigner : true, isWritable : true },
            // The program state account, which identifies the admin
            { pubkey : g_program_state_pubkey, isSigner : false, isWritable : false },
            // The admin, who receives the purchase price of a non-mystery
            { pubkey : new SolanaWeb3.PublicKey(admin_address), isSigner : false, isWritable : true },
            // The program authority, which holds the purchase price of a mystery in escrow
            { pubkey : g_program_authority_pubkey, isSigner : false, isWritable : true },
            // The block
            { pubkey : make_pubkey(entry.block.address), isSigner : false, isWritable : true },
            // The block's whitelist, whose entry for the funding account is used up if the block is in its whitelist
            // period
            { pubkey : make_pubkey(get_whitelist_address(entry.block.address)), isSigner : false, isWritable : true },
            // The entry
            { pubkey : make_pubkey(entry.address), isSigner : false, isWritable : true },
            // The entry's own token account, which holds the token until now and is closed
            { pubkey : make_pubkey(entry.token_address), isSigner : false, isWritable : true },
            // The entry's mint
            { pubkey : make_pubkey(entry.mint_address), isSigner : false, isWritable : false },
            // The wallet's associated token account for the entry's mint, which receives the token, and is created if
            // it does not exist yet
            { pubkey : make_pubkey(get_associated_token_address(wallet_address, entry.mint_address)),
              isSigner : false, isWritable : true },
            // The owner of that token account
            { pubkey : make_pubkey(wallet_address), isSigner : false, isWritable : false },
            // This program
            { pubkey : g_self_program_pubkey, isSigner : false, isWritable : false },
            // The SPL Token program
            { pubkey : g_spl_token_program_pubkey, isSigner : false, isWritable : false },
            // The Associated Token Account program
            { pubkey : g_spl_associated_token_program_pubkey, isSigner : false, isWritable : false },
            // The system program
            { pubkey : g_system_program_pubkey, isSigner : false, isWritable : false }
        ];

        // The instruction data is the instruction code, 7 bytes of padding (the program's BuyData struct aligns its
        // u64 to 8 bytes), and then the maximum price in lamports
        let data = Buffer.Buffer.from([ INSTRUCTION_BUY, 0, 0, 0, 0, 0, 0, 0,
                                        ...u64_to_le_bytes(maximum_price_lamports) ]);

        let tx = new SolanaWeb3.Transaction();

        tx.add(new SolanaWeb3.TransactionInstruction({ keys : keys, programId : g_self_program_pubkey, data : data }));

        return tx;
    }

    // Accounts and data here must match those of scripts/user_refund_tx.sh.  token_address is the token account that
    // holds the entry's token; if it is not given, the wallet's associated token account for the entry's mint is used.
    async make_refund_tx(entry, wallet_address, token_address)
    {
        if (token_address == null) {
            token_address = get_associated_token_address(wallet_address, entry.mint_address);
        }

        let keys = [
            // The token owner, who must sign
            { pubkey : make_pubkey(wallet_address), isSigner : true, isWritable : false },
            // The block
            { pubkey : make_pubkey(entry.block.address), isSigner : false, isWritable : false },
            // The entry
            { pubkey : make_pubkey(entry.address), isSigner : false, isWritable : true },
            // The program authority, which holds the purchase price in escrow and pays the refund
            { pubkey : g_program_authority_pubkey, isSigner : false, isWritable : true },
            // The token account holding the entry's token
            { pubkey : make_pubkey(token_address), isSigner : false, isWritable : false },
            // The account that receives the refund
            { pubkey : make_pubkey(wallet_address), isSigner : false, isWritable : true }
        ];

        // The instruction data is just the instruction code
        let data = Buffer.Buffer.from([ INSTRUCTION_REFUND ]);

        let tx = new SolanaWeb3.Transaction();

        tx.add(new SolanaWeb3.TransactionInstruction({ keys : keys, programId : g_self_program_pubkey, data : data }));

        return tx;
    }

    // Accounts and data here must match those of scripts/user_bid_tx.sh
    async make_bid_tx(entry, minimum_bid_lamports, maximum_bid_lamports, wallet_address)
    {
        let bid_marker_token_address = get_bid_marker_token_address(entry.mint_address, wallet_address);

        let keys = [
            // The bidding account, which pays the bid
            { pubkey : make_pubkey(wallet_address), isSigner : true, isWritable : true },
            // The entry
            { pubkey : make_pubkey(entry.address), isSigner : false, isWritable : true },
            // The bid marker mint
            { pubkey : g_bid_marker_mint_pubkey, isSigner : false, isWritable : true },
            // The bidder's bid marker token account for the entry
            { pubkey : make_pubkey(bid_marker_token_address), isSigner : false, isWritable : true },
            // The bid account, which holds the bid
            { pubkey : make_pubkey(get_bid_address(bid_marker_token_address)), isSigner : false, isWritable : true },
            // The program authority
            { pubkey : g_program_authority_pubkey, isSigner : false, isWritable : false },
            // The system program
            { pubkey : g_system_program_pubkey, isSigner : false, isWritable : false },
            // This program
            { pubkey : g_self_program_pubkey, isSigner : false, isWritable : false },
            // The SPL Token program
            { pubkey : g_spl_token_program_pubkey, isSigner : false, isWritable : false }
        ];

        // The instruction data is the instruction code, 7 bytes of padding (the program's BidData struct aligns its
        // u64s to 8 bytes), and then the minimum and maximum bids in lamports
        let data = Buffer.Buffer.from([ INSTRUCTION_BID, 0, 0, 0, 0, 0, 0, 0,
                                        ...u64_to_le_bytes(minimum_bid_lamports),
                                        ...u64_to_le_bytes(maximum_bid_lamports) ]);

        let tx = new SolanaWeb3.Transaction();

        tx.add(new SolanaWeb3.TransactionInstruction({ keys : keys, programId : g_self_program_pubkey, data : data }));

        return tx;
    }

    async make_claim_tx(entry, won, wallet_address)
    {
        if (won) {
            let admin_address = await this.fetch_admin_address();

            if (admin_address == null) {
                throw new Error("Admin address could not be determined");
            }

            // Claiming a winning bid is settling it, with the wallet as both the funding account and the bidder
            return this.make_settle_winning_bid_tx(entry, wallet_address, admin_address, wallet_address);
        }
        else {
            return this.make_claim_losing_tx(entry, wallet_address);
        }
    }

    // Accounts and data here must match those of scripts/user_claim_losing_tx.sh.  The bid marker is reclaimed too
    // (its tokens are burned and its token account closed), unless keep_bid_marker is true; keep_bid_marker is needed
    // only if the bidder has already burned or closed their bid marker, which would make reclaiming it fail.
    async make_claim_losing_tx(entry, wallet_address, keep_bid_marker)
    {
        let bid_marker_token_address = get_bid_marker_token_address(entry.mint_address, wallet_address);

        let keys = [
            // The bidding account, which receives the bid back
            { pubkey : make_pubkey(wallet_address), isSigner : true, isWritable : true },
            // The entry
            { pubkey : make_pubkey(entry.address), isSigner : false, isWritable : false },
            // The bid account, which is closed
            { pubkey : make_pubkey(get_bid_address(bid_marker_token_address)), isSigner : false, isWritable : true }
        ];

        if (!keep_bid_marker) {
            // The bid marker mint
            keys.push({ pubkey : g_bid_marker_mint_pubkey, isSigner : false, isWritable : true });
            // The bidder's bid marker token account for the entry
            keys.push({ pubkey : make_pubkey(bid_marker_token_address), isSigner : false, isWritable : true });
            // The program authority
            keys.push({ pubkey : g_program_authority_pubkey, isSigner : false, isWritable : false });
            // The SPL Token program
            keys.push({ pubkey : g_spl_token_program_pubkey, isSigner : false, isWritable : false });
        }

        // The instruction data is just the instruction code
        let data = Buffer.Buffer.from([ INSTRUCTION_CLAIM_LOSING ]);

        let tx = new SolanaWeb3.Transaction();

        tx.add(new SolanaWeb3.TransactionInstruction({ keys : keys, programId : g_self_program_pubkey, data : data }));

        return tx;
    }

    // Accounts and data here must match those of scripts/user_stake_tx.sh.  The wallet must be both the owner of the
    // entry's token (held in its associated token account) and the withdraw authority of the stake account.
    async make_stake_tx(entry, stake_address, wallet_address)
    {
        let keys = [
            // The block
            { pubkey : make_pubkey(entry.block.address), isSigner : false, isWritable : false },
            // The entry
            { pubkey : make_pubkey(entry.address), isSigner : false, isWritable : true },
            // The token owner
            { pubkey : make_pubkey(wallet_address), isSigner : true, isWritable : false },
            // The token account holding the entry's token, which is frozen for as long as the entry is staked
            { pubkey : make_pubkey(get_associated_token_address(wallet_address, entry.mint_address)),
              isSigner : false, isWritable : true },
            // The stake account
            { pubkey : make_pubkey(stake_address), isSigner : false, isWritable : true },
            // The withdraw authority of the stake account
            { pubkey : make_pubkey(wallet_address), isSigner : true, isWritable : false },
            // The Shinobi Systems vote account
            { pubkey : make_pubkey(g_shinobi_systems_vote_address), isSigner : false, isWritable : false },
            // The program authority, which becomes the stake account's authority
            { pubkey : g_program_authority_pubkey, isSigner : false, isWritable : false },
            // Clock sysvar
            { pubkey : g_clock_sysvar_pubkey, isSigner : false, isWritable : false },
            // The stake program
            { pubkey : g_stake_program_pubkey, isSigner : false, isWritable : false },
            // The stake config account, required by the stake program
            { pubkey : g_stake_config_pubkey, isSigner : false, isWritable : false },
            // Stake history sysvar, required by the stake program
            { pubkey : g_stake_history_sysvar_pubkey, isSigner : false, isWritable : false },
            // The entry's mint, needed to freeze the token account
            { pubkey : make_pubkey(entry.mint_address), isSigner : false, isWritable : false },
            // The SPL Token program
            { pubkey : g_spl_token_program_pubkey, isSigner : false, isWritable : false }
        ];

        // The instruction data is just the instruction code
        let data = Buffer.Buffer.from([ INSTRUCTION_STAKE ]);

        let tx = new SolanaWeb3.Transaction();

        tx.add(new SolanaWeb3.TransactionInstruction({ keys : keys, programId : g_self_program_pubkey, data : data }));

        return tx;
    }

    // Accounts and data here must match those of scripts/user_destake_tx.sh
    async make_destake_tx(entry, wallet_address, no_ki_harvest, no_thaw)
    {
        let keys = [
            // The funding account, which pays for the Ki destination account if it does not exist yet
            { pubkey : make_pubkey(wallet_address), isSigner : true, isWritable : true },
            // The block
            { pubkey : make_pubkey(entry.block.address), isSigner : false, isWritable : false },
            // The entry
            { pubkey : make_pubkey(entry.address), isSigner : false, isWritable : true },
            // The token owner, who receives the stake account
            { pubkey : make_pubkey(wallet_address), isSigner : true, isWritable : false },
            // The token account holding the entry's token, which is thawed unless no_thaw
            { pubkey : make_pubkey(get_associated_token_address(wallet_address, entry.mint_address)),
              isSigner : false, isWritable : true },
            // The entry's stake account
            { pubkey : make_pubkey(entry.owned_stake_account), isSigner : false, isWritable : true },
            // The Ki destination account, which receives harvested Ki
            { pubkey : make_pubkey(get_associated_token_address(wallet_address, g_ki_mint_address)),
              isSigner : false, isWritable : true },
            // The owner of the Ki destination account
            { pubkey : make_pubkey(wallet_address), isSigner : false, isWritable : false },
            // The Ki mint
            { pubkey : g_ki_mint_pubkey, isSigner : false, isWritable : true },
            // The program authority
            { pubkey : g_program_authority_pubkey, isSigner : false, isWritable : false },
            // Clock sysvar
            { pubkey : g_clock_sysvar_pubkey, isSigner : false, isWritable : false },
            // The system program
            { pubkey : g_system_program_pubkey, isSigner : false, isWritable : false },
            // The stake program
            { pubkey : g_stake_program_pubkey, isSigner : false, isWritable : false },
            // The SPL Token program
            { pubkey : g_spl_token_program_pubkey, isSigner : false, isWritable : false },
            // The Associated Token Account program
            { pubkey : g_spl_associated_token_program_pubkey, isSigner : false, isWritable : false },
            // The entry's mint, which is needed to thaw the token account unless no_thaw
            { pubkey : make_pubkey(entry.mint_address), isSigner : false, isWritable : false }
        ];

        // The instruction data is the instruction code, then the no_ki_harvest flag (1 = do not harvest Ki), then the
        // no_thaw flag (1 = leave the token account frozen)
        let data = Buffer.Buffer.from([ INSTRUCTION_DESTAKE, (no_ki_harvest ? 1 : 0), (no_thaw ? 1 : 0) ]);

        let tx = new SolanaWeb3.Transaction();

        tx.add(new SolanaWeb3.TransactionInstruction({ keys : keys, programId : g_self_program_pubkey, data : data }));

        return tx;
    }

    // Accounts and data here must match those of scripts/user_thaw_tx.sh
    async make_thaw_tx(entry, wallet_address)
    {
        let keys = [
            // The block
            { pubkey : make_pubkey(entry.block.address), isSigner : false, isWritable : false },
            // The entry
            { pubkey : make_pubkey(entry.address), isSigner : false, isWritable : false },
            // The token owner
            { pubkey : make_pubkey(wallet_address), isSigner : true, isWritable : false },
            // The token account holding the entry's token, which is thawed
            { pubkey : make_pubkey(get_associated_token_address(wallet_address, entry.mint_address)),
              isSigner : false, isWritable : true },
            // The entry's mint
            { pubkey : make_pubkey(entry.mint_address), isSigner : false, isWritable : false },
            // The program authority, which is the freeze authority of the entry's mint
            { pubkey : g_program_authority_pubkey, isSigner : false, isWritable : false },
            // The SPL Token program
            { pubkey : g_spl_token_program_pubkey, isSigner : false, isWritable : false }
        ];

        // The instruction data is just the instruction code
        let data = Buffer.Buffer.from([ INSTRUCTION_THAW ]);

        let tx = new SolanaWeb3.Transaction();

        tx.add(new SolanaWeb3.TransactionInstruction({ keys : keys, programId : g_self_program_pubkey, data : data }));

        return tx;
    }

    // Accounts and data here must match those of scripts/user_harvest_tx.sh
    async make_harvest_tx(entry, wallet_address)
    {
        let keys = [
            // The funding account, which pays for the Ki destination account if it does not exist yet
            { pubkey : make_pubkey(wallet_address), isSigner : true, isWritable : true },
            // The entry
            { pubkey : make_pubkey(entry.address), isSigner : false, isWritable : true },
            // The token owner
            { pubkey : make_pubkey(wallet_address), isSigner : true, isWritable : false },
            // The token account holding the entry's token
            { pubkey : make_pubkey(get_associated_token_address(wallet_address, entry.mint_address)),
              isSigner : false, isWritable : false },
            // The entry's stake account
            { pubkey : make_pubkey(entry.owned_stake_account), isSigner : false, isWritable : false },
            // The Ki destination account, which receives harvested Ki
            { pubkey : make_pubkey(get_associated_token_address(wallet_address, g_ki_mint_address)),
              isSigner : false, isWritable : true },
            // The owner of the Ki destination account
            { pubkey : make_pubkey(wallet_address), isSigner : false, isWritable : false },
            // The Ki mint
            { pubkey : g_ki_mint_pubkey, isSigner : false, isWritable : true },
            // The program authority
            { pubkey : g_program_authority_pubkey, isSigner : false, isWritable : false },
            // The system program
            { pubkey : g_system_program_pubkey, isSigner : false, isWritable : false },
            // The SPL Token program
            { pubkey : g_spl_token_program_pubkey, isSigner : false, isWritable : false },
            // The Associated Token Account program
            { pubkey : g_spl_associated_token_program_pubkey, isSigner : false, isWritable : false },
            // The entry's block, whose block_start_timestamp is the beginning of the entry's life span
            { pubkey : make_pubkey(entry.block.address), isSigner : false, isWritable : false }
        ];

        // The instruction data is just the instruction code
        let data = Buffer.Buffer.from([ INSTRUCTION_HARVEST ]);

        let tx = new SolanaWeb3.Transaction();

        tx.add(new SolanaWeb3.TransactionInstruction({ keys : keys, programId : g_self_program_pubkey, data : data }));

        return tx;
    }

    // Accounts and data here must match those of scripts/user_level_up_tx.sh.  ki_to_apply is the most whole Ki to
    // burn; only as much of it as earns level credits is burned.  The Ki comes from the wallet's associated Ki token
    // account, which LevelUp creates (paid for by the wallet) if it does not exist yet.
    async make_level_up_tx(entry, ki_to_apply, wallet_address)
    {
        let keys = [
            // The entry
            { pubkey : make_pubkey(entry.address), isSigner : false, isWritable : true },
            // The token owner
            { pubkey : make_pubkey(wallet_address), isSigner : true, isWritable : false },
            // The token account holding the entry's token
            { pubkey : make_pubkey(get_associated_token_address(wallet_address, entry.mint_address)),
              isSigner : false, isWritable : false },
            // The entry's Metaplex metadata, which is updated to the new level
            { pubkey : make_pubkey(entry.metaplex_metadata_address), isSigner : false, isWritable : true },
            // The Ki source account, from which Ki is burned
            { pubkey : make_pubkey(get_associated_token_address(wallet_address, g_ki_mint_address)),
              isSigner : false, isWritable : true },
            // The owner of the Ki source account, which pays for that account if it has to be created
            { pubkey : make_pubkey(wallet_address), isSigner : true, isWritable : true },
            // The Ki mint
            { pubkey : g_ki_mint_pubkey, isSigner : false, isWritable : true },
            // The program authority
            { pubkey : g_program_authority_pubkey, isSigner : false, isWritable : false },
            // The SPL Token program
            { pubkey : g_spl_token_program_pubkey, isSigner : false, isWritable : false },
            // The Metaplex program
            { pubkey : g_metaplex_program_pubkey, isSigner : false, isWritable : false },
            // The entry's stake account; for an entry that is not staked this is the system program address, which is
            // the placeholder that LevelUp expects
            { pubkey : make_pubkey(entry.owned_stake_account), isSigner : false, isWritable : false },
            // The system program
            { pubkey : g_system_program_pubkey, isSigner : false, isWritable : false },
            // The Associated Token Account program
            { pubkey : g_spl_associated_token_program_pubkey, isSigner : false, isWritable : false },
            // The entry's block, whose block_start_timestamp is the beginning of the entry's life span; LevelUp only
            // uses it if the entry is staked
            { pubkey : make_pubkey(entry.block.address), isSigner : false, isWritable : false }
        ];

        // The instruction data is the instruction code, 7 bytes of padding (the program's LevelUpData struct aligns
        // its u64 to 8 bytes), and then the most Ki to apply
        let data = Buffer.Buffer.from([ INSTRUCTION_LEVEL_UP, 0, 0, 0, 0, 0, 0, 0, ...u64_to_le_bytes(ki_to_apply) ]);

        let tx = new SolanaWeb3.Transaction();

        tx.add(new SolanaWeb3.TransactionInstruction({ keys : keys, programId : g_self_program_pubkey, data : data }));

        return tx;
    }

    // Accounts and data here must match those of scripts/anyone_settle_winning_bid_tx.sh.  admin_address may be
    // anything that SolanaWeb3.PublicKey accepts, including the bytes returned by fetch_admin_address().
    //
    // If the wallet is the bidder, the bid marker is reclaimed too, unless keep_bid_marker is true; keep_bid_marker
    // is needed only if the bidder has already burned or closed their bid marker, which would make reclaiming it fail.
    async make_settle_winning_bid_tx(entry, bidder_address, admin_address, wallet_address, keep_bid_marker)
    {
        // The bid account of the bidder for this entry
        let bid_address = get_bid_address(get_bid_marker_token_address(entry.mint_address, bidder_address));

        let keys = [
            // The funding account, which pays for the bidder's associated token account if it does not exist yet
            { pubkey : make_pubkey(wallet_address), isSigner : true, isWritable : true },
            // The entry
            { pubkey : make_pubkey(entry.address), isSigner : false, isWritable : true },
            // The winning bid account
            { pubkey : make_pubkey(bid_address), isSigner : false, isWritable : true },
            // The program state account, which identifies the admin
            { pubkey : g_program_state_pubkey, isSigner : false, isWritable : false },
            // The admin, who receives the winning bid
            { pubkey : new SolanaWeb3.PublicKey(admin_address), isSigner : false, isWritable : true },
            // The entry's own token account, which holds the token until now and is closed
            { pubkey : make_pubkey(entry.token_address), isSigner : false, isWritable : true },
            // The entry's mint
            { pubkey : make_pubkey(entry.mint_address), isSigner : false, isWritable : false },
            // The program authority
            { pubkey : g_program_authority_pubkey, isSigner : false, isWritable : false },
            // The bidder's associated token account for the entry's mint, which receives the token
            { pubkey : make_pubkey(get_associated_token_address(bidder_address, entry.mint_address)),
              isSigner : false, isWritable : true },
            // The bidder, who receives any lamports that the bid account holds beyond the winning bid
            { pubkey : make_pubkey(bidder_address), isSigner : false, isWritable : true },
            // The system program
            { pubkey : g_system_program_pubkey, isSigner : false, isWritable : false },
            // The SPL Token program
            { pubkey : g_spl_token_program_pubkey, isSigner : false, isWritable : false },
            // The Associated Token Account program
            { pubkey : g_spl_associated_token_program_pubkey, isSigner : false, isWritable : false }
        ];

        // When the bidder settles their own bid, also reclaim the bid marker, which has no purpose once the bid is
        // settled: two more accounts make the program burn the bidder's bid marker tokens for the entry and close the
        // bid marker token account, returning its lamports to the bidder.  Nobody else can do this, since it needs
        // the bidder's signature.
        if ((wallet_address == bidder_address) && !keep_bid_marker) {
            // The bid marker mint
            keys.push({ pubkey : g_bid_marker_mint_pubkey, isSigner : false, isWritable : true });
            // The bidder's bid marker token account for the entry
            keys.push({ pubkey : make_pubkey(get_bid_marker_token_address(entry.mint_address, bidder_address)),
                        isSigner : false, isWritable : true });
        }

        // The instruction data is just the instruction code
        let data = Buffer.Buffer.from([ INSTRUCTION_SETTLE_WINNING_BID ]);

        let tx = new SolanaWeb3.Transaction();

        tx.add(new SolanaWeb3.TransactionInstruction({ keys : keys, programId : g_self_program_pubkey, data : data }));

        return tx;
    }

    // Accounts and data here must match those of scripts/anyone_delegate_tx.sh
    async make_delegate_tx(entry, wallet_address)
    {
        let keys = [
            // The block
            { pubkey : make_pubkey(entry.block.address), isSigner : false, isWritable : false },
            // The entry
            { pubkey : make_pubkey(entry.address), isSigner : false, isWritable : true },
            // The entry's stake account
            { pubkey : make_pubkey(entry.owned_stake_account), isSigner : false, isWritable : true },
            // The program authority, which is the stake authority of the stake account
            { pubkey : g_program_authority_pubkey, isSigner : false, isWritable : false },
            // Clock sysvar, required by the stake program
            { pubkey : g_clock_sysvar_pubkey, isSigner : false, isWritable : false },
            // The stake program
            { pubkey : g_stake_program_pubkey, isSigner : false, isWritable : false },
            // Stake history sysvar, required by the stake program
            { pubkey : g_stake_history_sysvar_pubkey, isSigner : false, isWritable : false },
            // The Shinobi Systems vote account
            { pubkey : make_pubkey(g_shinobi_systems_vote_address), isSigner : false, isWritable : false },
            // The stake config account, required by the stake program
            { pubkey : g_stake_config_pubkey, isSigner : false, isWritable : false }
        ];

        // The instruction data is just the instruction code
        let data = Buffer.Buffer.from([ INSTRUCTION_DELEGATE ]);

        let tx = new SolanaWeb3.Transaction();

        tx.add(new SolanaWeb3.TransactionInstruction({ keys : keys, programId : g_self_program_pubkey, data : data }));

        return tx;
    }

    // Returns the UpdateEpochsStaked instruction for the entry, which brings its epochs_staked up to date.  This is
    // provided separately from make_update_epochs_staked_tx so that another program's transaction (for example, a
    // game's) can include it before its own instruction, after which that program can rely on the entry's
    // epochs_staked being exact.  Accounts and data here must match those of scripts/anyone_update_epochs_staked_tx.sh
    make_update_epochs_staked_instruction(entry)
    {
        let keys = [
            // The entry
            { pubkey : make_pubkey(entry.address), isSigner : false, isWritable : true },
            // The entry's stake account; for an entry that is not staked this is the system program address, which is
            // a placeholder that UpdateEpochsStaked does not use
            { pubkey : make_pubkey(entry.owned_stake_account), isSigner : false, isWritable : false }
        ];

        // The instruction data is just the instruction code
        let data = Buffer.Buffer.from([ INSTRUCTION_UPDATE_EPOCHS_STAKED ]);

        return new SolanaWeb3.TransactionInstruction({ keys : keys, programId : g_self_program_pubkey, data : data });
    }

    async make_update_epochs_staked_tx(entry, wallet_address)
    {
        let tx = new SolanaWeb3.Transaction();

        tx.add(this.make_update_epochs_staked_instruction(entry));

        return tx;
    }

    // Accounts and data here must match those of scripts/admin_withdraw_excess_tx.sh
    async make_withdraw_excess_tx(account_type, account, wallet_address)
    {
        // A Block or Entry may be passed in, or just an address
        let account_address = ((account != null) && (account.address != null)) ? account.address : account;

        // Find the address of the account to withdraw from
        let from_address;
        switch (account_type) {
        case WithdrawExcessAccountType.ProgramState:
            from_address = g_program_state_address;
            break;
        case WithdrawExcessAccountType.Authority:
            from_address = g_program_authority_address;
            break;
        case WithdrawExcessAccountType.Block:
        case WithdrawExcessAccountType.Entry:
            from_address = account_address;
            break;
        case WithdrawExcessAccountType.Whitelist:
            // The whitelist account is derived from the address of its block
            from_address = (account_address == null) ? null : get_whitelist_address(account_address);
            break;
        default:
            throw new Error("Invalid account type");
        }

        if (from_address == null) {
            throw new Error("No account to withdraw from was provided for account type " + account_type.name);
        }

        let keys = [
            // The program state account, which identifies the admin
            { pubkey : g_program_state_pubkey, isSigner : false, isWritable : false },
            // The admin, who must sign and who receives the lamports
            { pubkey : make_pubkey(wallet_address), isSigner : true, isWritable : true },
            // The account to withdraw from
            { pubkey : make_pubkey(from_address), isSigner : false, isWritable : true }
        ];

        // The instruction data is the instruction code followed by the code of the account type
        let data = Buffer.Buffer.from([ INSTRUCTION_WITHDRAW_EXCESS, account_type.code ]);

        let tx = new SolanaWeb3.Transaction();

        tx.add(new SolanaWeb3.TransactionInstruction({ keys : keys, programId : g_self_program_pubkey, data : data }));

        return tx;
    }

    async complete_tx(tx_maker_func, sign_callback)
    {
        let wallet_address_holder = this.wallet_address_holder;
        let wallet_address = wallet_address_holder.wallet_address;
        if (wallet_address == null) {
            throw new Error("No wallet");
        }

        let wallet_pubkey = make_pubkey(wallet_address);

        while (true) {
            let tx = await tx_maker_func(wallet_address);

            if (wallet_address_holder != this.wallet_address_holder) {
                // Wallet address changed, so abort
                throw new Error("Wallet change");
            }

            let recent_blockhash_result = await this.cluster.rpc_connections.run((rpc_connection) =>
                {
                    return rpc_connection.getLatestBlockhash();
                }, "fetch recent blockhash");

            if (wallet_address_holder != this.wallet_address_holder) {
                // Wallet address changed, so abort
                throw new Error("Wallet change");
            }

            if (recent_blockhash_result == null) {
                throw new Error("Failed to fetch recent_blockhash_values");
            }

            let recent_blockhash = recent_blockhash_result.blockhash;

            // It appears that last valid block height is not reported correctly!  Use 120 ...
            // let recent_blockhash_expiry = recent_blockhash_result.lastValidBlockHeight;
            let recent_blockhash_expiry = 120;

            tx.feePayer = wallet_pubkey;
            tx.recentBlockhash = recent_blockhash;

            let tx_base64 = Buffer.Buffer.from(tx.serialize({ verifySignatures : false })).toString("base64");

            let result = await sign_callback(tx_base64, recent_blockhash_expiry);

            if (wallet_address_holder != this.wallet_address_holder) {
                // Wallet address changed, so abort
                throw new Error("Wallet change");
            }

            // Return value 0 means abort
            if (result == 0) {
                return null;
            }
            // Return value 1 means repeat with new recent blockhash
            else if (result == 1) {
                continue;
            }

            // Else return value is base64 encoded signature to use
            try {
                tx.addSignature(wallet_pubkey, bs58.decode(result));
            }
            catch (error) {
                throw new Error("Invalid signature: " + error);
            }

            let raw_tx;
            try {
                raw_tx = tx.serialize();
            }
            catch (error) {
                throw new Error("Invalid transaction: " + error);
            }

            // Submit the tx
            return this.submit_tx(raw_tx);
        }
    }

    async submit_tx(raw_tx)
    {
        // Retry up to 4 times with a 1 second delay in between each retry
        let retry_count = 0;
        while (true) {
            try {
                return await this.cluster.rpc_connections.run_once((rpc_connection) => {
                    return rpc_connection.sendRawTransaction(raw_tx);
                }, "submit tx");
            }
            catch (error) {
                if (retry_count++ == 4) {
                    throw new Error("Failed to submit transaction: " + error);
                }
                await delay(1000);
            }
        }
    }
}


function get_metaplex_metadata_address(mint_address)
{
    return find_pda([ Buffer.Buffer.from("metadata"),
                      g_metaplex_program_pubkey.toBuffer(),
                      address_to_buffer(mint_address) ],
                    g_metaplex_program_pubkey)[0].toBase58();
}


function get_block_address(group_number, block_number)
{
    return find_pda([ [ 10 ],
                      u32_to_le_bytes(group_number),
                      u32_to_le_bytes(block_number) ],
                    g_self_program_pubkey)[0].toBase58();
}


function get_whitelist_address(block_address)
{
    return find_pda([ [ 9 ],
                      address_to_buffer(block_address) ],
                    g_self_program_pubkey)[0].toBase58();
}


function get_entry_mint_address(block_address, entry_index)
{
    return find_pda([ [ 4 ],
                      address_to_buffer(block_address),
                      u16_to_le_bytes(entry_index) ],
                    g_self_program_pubkey)[0].toBase58();
}


function get_entry_address(entry_mint_address)
{
    return find_pda([ [ 11 ],
                      address_to_buffer(entry_mint_address) ],
                    g_self_program_pubkey)[0].toBase58();
}


function get_associated_token_address(token_owner_address, token_mint_address)
{
    return find_pda([ address_to_buffer(token_owner_address),
                      g_spl_token_program_pubkey.toBuffer(),
                      address_to_buffer(token_mint_address) ],
                    g_spl_associated_token_program_pubkey)[0].toBase58();
}


function get_bid_marker_token_address(entry_mint_address, bidder_address)
{
    return find_pda([ [ 8 ],
                      address_to_buffer(entry_mint_address),
                      address_to_buffer(bidder_address) ],
                    g_self_program_pubkey)[0].toBase58();
}


function get_bid_address(bid_marker_token_address)
{
    return find_pda([ [ 6 ],
                      address_to_buffer(bid_marker_token_address) ],
                    g_self_program_pubkey)[0].toBase58();
}


// Returns the excess of an account that holds lamports and is required to hold required_lamports (both BigInt): the
// amount that the WithdrawExcess instruction would withdraw from it, which is zero if it holds no more than is
// required
function excess_of(lamports, required_lamports)
{
    return (lamports > required_lamports) ? (lamports - required_lamports) : 0n;
}


function buffer_le_u64(buffer, offset)
{
    let ret = BigInt(0);

    for (let i = 8; i > 0; i -= 1) {
        ret = ret * 256n;
        ret += BigInt((buffer[offset + (i - 1)] & 0xFF));
    }

    return ret;
}


function buffer_le_s64(buffer, offset)
{
    return BigInt.asIntN(64, buffer_le_u64(buffer, offset));
}


// Reads a little-endian u32.  JavaScript bitwise operators work on signed 32-bit values (and take shift counts modulo
// 32), so the most significant byte is shifted by 24, and the final ">>> 0" turns the signed result into an unsigned
// one, so that values >= 2^31 are not negative.
function buffer_le_u32(buffer, offset)
{
    return ((((buffer[offset + 0] & 0xFF) <<  0) |
             ((buffer[offset + 1] & 0xFF) <<  8) |
             ((buffer[offset + 2] & 0xFF) << 16) |
             ((buffer[offset + 3] & 0xFF) << 24)) >>> 0);
}


function buffer_le_u16(buffer, offset)
{
    return (((buffer[offset + 0] & 0xFF) << 0) |
            ((buffer[offset + 1] & 0xFF) << 8));
}


function buffer_sha256(buffer, offset)
{
    return buffer.slice(offset, offset + 32).toString("hex");
}


function buffer_address(buffer, offset)
{
    return bs58.encode(buffer.slice(offset, offset + 32));
}


function buffer_string(buffer, offset, length)
{
    let result = new TextDecoder("utf-8").decode(buffer.slice(offset, offset + length));

    // To remove extra nulls, or add a null if one wasn't present
    let index = result.indexOf('\0');

    if (index >= 0) {
        return result.substring(0, index);
    }
    else {
        return result + '\0';
    }
}


// Returns the 4 little-endian bytes of a u32.  ">>>" (unsigned shift) is used so that values >= 2^31 are handled
// correctly.
function u32_to_le_bytes(u)
{
    return [ (u >>>  0) & 0xFF,
             (u >>>  8) & 0xFF,
             (u >>> 16) & 0xFF,
             (u >>> 24) & 0xFF ];
}


// Returns the 8 little-endian bytes of a u64.  u may be a BigInt, a number, or a decimal string (use a string or a
// BigInt for values that do not fit in 53 bits).  Throws if u is negative or does not fit in 64 bits.
function u64_to_le_bytes(u)
{
    let value = BigInt(u);

    if ((value < 0n) || (value > 0xFFFFFFFFFFFFFFFFn)) {
        throw new Error("Value out of u64 range: " + u);
    }

    let ret = [ ];

    for (let i = 0; i < 8; i += 1) {
        ret.push(Number(value & 0xFFn));
        value >>= 8n;
    }

    return ret;
}


function u16_to_le_bytes(u)
{
    return [ (u >>  0) & 0xFF,
             (u >>  8) & 0xFF ];
}


exports.RpcConnections = RpcConnections;
exports.Cluster = Cluster;
exports.Block = Block;
exports.EntryState = EntryState;
exports.Entry = Entry;
exports.Wallet = Wallet;
exports.WithdrawExcessAccountType = WithdrawExcessAccountType;
