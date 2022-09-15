'use strict';

const SolanaWeb3 = require("@solana/web3.js");
const Buffer = require("buffer");
const bs58 = require("bs58");
const { _buy_tx,
        _refund_tx,
        _bid_tx,
        _claim_losing_tx,
        _claim_winning_tx,
        _stake_tx,
        _destake_tx,
        _harvest_tx,
        _level_up_tx,
        _take_commission_or_delegate_tx
      } = require("./tx.js");

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

const g_nifty_program_address = "shin1Sf5v4WNGKCCTQWFUdQEBGXZZ2J1eGuM3ueR2fa";
const g_nifty_program_pubkey = make_pubkey(g_nifty_program_address);

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

const g_nifty_program_authority_pubkey = find_pda([ [ 2 ] ], g_nifty_program_pubkey)[0];
const g_nifty_program_authority_address = g_nifty_program_authority_pubkey.toBase58();
      
const g_config_pubkey = find_pda([ [ 1 ] ], g_nifty_program_pubkey)[0];
const g_config_address = g_config_pubkey.toBase58();

const g_master_stake_pubkey = find_pda([ [ 3 ] ], g_nifty_program_pubkey)[0];
const g_master_stake_address = g_master_stake_pubkey.toBase58();

const g_ki_mint_pubkey = find_pda([ [ 4 ] ], g_nifty_program_pubkey)[0];
const g_ki_mint_address = g_ki_mint_pubkey.toBase58();

const g_ki_metadata_address = get_metaplex_metadata_address(g_ki_mint_address);
const g_ki_metadata_pubkey = make_pubkey(g_ki_metadata_address);

const g_bid_marker_mint_pubkey = find_pda([ [ 11 ] ], g_nifty_program_pubkey)[0];
const g_bid_marker_mint_address = g_bid_marker_mint_pubkey.toBase58();


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
        return new RpcConnections(rpc_endpoint, max_requests, max_data);
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
    // If default_slot_duration_seconds is null, a default is used.  If slots_per_epoch is null, a default is used
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
        this.block_start_timestamp = Number(buffer_le_s64(data, 80));
        this.mysteries_sold_count = buffer_le_u16(data, 88);
        this.mystery_phase_end_timestamp = Number(buffer_le_s64(data, 96));
        this.commission = buffer_le_u16(data, 104);
        this.last_commission_change_epoch = Number(buffer_le_u64(data, 112));
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

        if (new_block.commission != this.commission) {
            this.commission = new_block.commission;
            changed = true;
        }

        // last_commission_change_epoch could theoretically change independently of commission, because could
        // query before and after the commission has changed and then changed back -- i.e. the ABA problem
        if (new_block.last_commission_change_epoch != this.last_commission_change_epoch) {
            this.last_commission_change_epoch = new_block.last_commission_change_epoch;
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
    OwnedAndStaked             : Symbol("OwnedAndStaked")
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
        this.commission = buffer_le_u16(data, 218);
        this.auction_highest_bid_lamports = buffer_le_u64(data, 224);
        this.auction_winning_bid_address = buffer_address(data, 232);
        this.owned_stake_account = buffer_address(data, 264);
        this.owned_stake_initial_lamports = buffer_le_u64(data, 296);
        this.owned_stake_epoch = Number(buffer_le_u64(data, 304));
        this.owned_last_ki_harvest_stake_account_lamports = buffer_le_u64(data, 312);
        this.owned_last_commission_charge_stake_account_lamports = buffer_le_u64(data, 320);
        this.level = data[328];
        
        this.metadata_level_1_ki = buffer_le_u32(data, 332);
        this.metadata_random = [ buffer_le_u32(data, 336),
                                 buffer_le_u32(data, 340),
                                 buffer_le_u32(data, 344),
                                 buffer_le_u32(data, 348),
                                 buffer_le_u32(data, 352),
                                 buffer_le_u32(data, 356),
                                 buffer_le_u32(data, 360),
                                 buffer_le_u32(data, 364),
                                 buffer_le_u32(data, 368),
                                 buffer_le_u32(data, 372),
                                 buffer_le_u32(data, 376),
                                 buffer_le_u32(data, 380),
                                 buffer_le_u32(data, 384),
                                 buffer_le_u32(data, 388),
                                 buffer_le_u32(data, 392),
                                 buffer_le_u32(data, 396) ];
        this.level_metadata = [ ];
        
        for (let i = 0; i < 9; i += 1) {
            this.level_metadata[i] = {
                form : data[400 + (i * 288)],
                skill : data[401 + (i * 288)],
                ki_factor : buffer_le_u32(data, 404 + (i * 288)),
                name : buffer_string(data, 408 + (i * 288), 48),
                uri : buffer_string(data, 456 + (i * 288), 200),
                uri_contents_sha256 : buffer_sha256(data, 656 + (i * 288))
            };
        }
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

        if (new_entry.refund_awarded != this.refund_awarded) {
            this.refund_awarded = new_entry.refund_awarded;
            changed = true;
        }
        
        if (new_entry.commission != this.commission) {
            this.commission = new_entry.commission;
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
        
        if (new_entry.owned_stake_initial_lamports != this.owned_stake_initial_lamports) {
            this.owned_stake_initial_lamports = new_entry.owned_stake_initial_lamports;
            changed = true;
        }
        
        if (new_entry.owned_stake_epoch != this.owned_stake_epoch) {
            this.owned_stake_epoch = new_entry.owned_stake_epoch;
            changed = true;
        }
        
        if (new_entry.owned_last_ki_harvest_stake_account_lamports !=
            this.owned_last_ki_harvest_stake_account_lamports) {
            this.owned_last_ki_harvest_stake_account_lamports = new_entry.owned_last_ki_harvest_stake_account_lamports;
            changed = true;
        }
        
        if (new_entry.owned_last_commission_charge_stake_account_lamports !=
            this.owned_last_commission_charge_stake_account_lamports) {
            this.owned_last_commission_charge_stake_account_lamports =
                new_entry.owned_last_commission_charge_stake_account_lamports;
            changed = true;
        }
        
        if (new_entry.level != this.level) {
            this.level = new_entry.level;
            changed = true;
        }

        let new_level_metadata = [ ];

        let level_metadata_changed = false;
        
        for (let i = 0; i < 9; i += 1) {
            new_level_metadata[i] = {
                form : data[400 + (i * 288)],
                skill : data[401 + (i * 288)],
                ki_factor : buffer_le_u32(data, 404 + (i * 288)),
                name : buffer_string(data, 408 + (i * 288), 48),
                uri : buffer_string(data, 456 + (i * 288), 200),
                uri_contents_sha256 : buffer_sha256(data, 656 + (i * 288))
            };
            if ((new_level_metadata[i].form != this.level_metadata[i].form) ||
                (new_level_metadata[i].skill != this.level_metadata[i].skill) ||
                (new_level_metadata[i].ki_factor != this.level_metadata[i].ki_factor) ||
                (new_level_metadata[i].name != this.level_metadata[i].name) ||
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

    // Cribbed from the nifty program's get_entry_state() function
    get_entry_state(clock)
    {
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
                    if ((this.reveal_timestamp + this.duration) > ((Date.now() / 1000) | 0)) {
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
        if (this.get_entry_state(this.block, clock) == EntryState.PreRevealUnowned) {
            return Entry.compute_price(BigInt(this.block.mystery_phase_duration),
                                       this.block.mystery_start_price_lamports,
                                       this.block.minimum_price_lamports,
                                       BigInt(clock.unix_timestamp - this.block.block_start_timestamp));
        }
        else if (this.has_auction) {
            return this.block.minimum_price_lamports;
        }
        else {
            return Entry.compute_price(BigInt(this.duration),
                                       this.non_auction_start_price_lamports,
                                       this.minimum_price_lamports,
                                       BigInt(clock.unix_timestamp - this.reveal_timestamp));
        }
    }

    // This only returns a valid value if the entry state is InAuction
    get_auction_minimum_bid_lamports(clock)
    {
        return Entry.compute_minimum_bid(BigInt(this.duration),
                                         this.minimum_price_lamports,
                                         this.auction_highest_bid_lamports,
                                         BigInt(clock.unix_timestamp - this.reveal_timestamp));
    }

    // This only returns a valid value if the entry state is InAuction
    get_auction_end_unix_timestamp(clock)
    {
        return this.reveal_timestamp + this.duration;
    }

    // This only returns a valid value if the entry state is WaitingForRevealOwned
    get_reveal_deadline()
    {
        return (this.block.mystery_phase_end_timestamp + this.block.reveal_period_duration);
    }

    // Returns the amount of Ki available for harvest from this entry
    async get_harvestable_ki(rpc_connections)
    {
        // If it's not revealed yet, it couldn't be staked
        if ((this.reveal_sha256 != "0000000000000000000000000000000000000000000000000000000000000000") ||
            // If it's not purchased yet, it couldn't be staked
            (this.purchase_price_lamports == 0) ||
            // If it has no stake account, then it's not staked
            (this.owned_stake_account == g_system_program_address)) {
            return 0;
        }

        let result = await rpc_connections.run((rpc_connection) => {
            return rpc_connection.getAccountInfo(this.owned_stake_account,
                                                 {
                                                     dataSlice : { offset : 156,
                                                                   length : 8 }
                                                 });
        });

        if ((result == null) || (result.data == null)) {
            return 0;
        }

        let stake = buffer_le_u64(result.data, 0);

        return (((Number(stake - this.owned_last_ki_harvest_stake_account_lamports) *
                  this.level_metadata[this.level].ki_factor) / LAMPORTS_PER_SOL) | 0);
    }

    // Private implementation follows ---------------------------------------------------------------------------------
    
    // Cribbed from user_buy.c
    static compute_price(total_seconds, start_price, end_price, seconds_elapsed)
    {
        if (seconds_elapsed >= total_seconds) {
            return end_price;
        }

        let delta = start_price - end_price;

        delta /= 1000n;
        end_price /= 1000n;

        let ac = delta * 101n;

        let ab = ((100n * delta * seconds_elapsed) / total_seconds) + delta;

        let bc = ((100n * 101n * seconds_elapsed) / total_seconds) + 101n;

        return (end_price + ((ac - ab) / bc)) * 1000n;
    }

    // Cribbed from user_bid.c
    static compute_minimum_bid(auction_duration, initial_minimum_bid, current_max_bid, seconds_elapsed)
    {
        if (current_max_bid < initial_minimum_bid) {
            current_max_bid = initial_minimum_bid;
        }

        if (seconds_elapsed > auction_duration) {
            seconds_elapsed = auction_duration;
        }

        let a = seconds_elapsed;
        let b = auction_duration;
        let p = current_max_bid;

        let result = (p * (((1000n * b) / ((b + (b / 100n)) - a)) + 101000n)) / 100000n;

        let min_result = current_max_bid + (current_max_bid / 50n);
        let max_result = (2n * current_max_bid) + (current_max_bid / 100n);
    
        if (result < min_result) {
            return min_result;
        }
        else if (result > max_result) {
            return max_result;
        }
        else {
            return result;
        }
    }
}


// View of a Cluster that supplies information about the contents of the wallet, and provides wallet-specific actions
// All addresss passed into and returned from Wallet are strings.
class Wallet
{
    static create(cluter, wallet_address)
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
        // xxx tokens

        // Fetch wallet stakes only once every 30 seconds
        // this.last_stakes_fetch_time = 0;
        // xxx stakes
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
                
                if (account.data.parsed.info.state != "initialized") {
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

            this.stakes_by_address = new Map();

            let now = Date.now();
            
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
                    this.stakes_by_address.set(stake.address, stake);
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
                    this.stakes_by_address.set(stake.address, stake);
                }
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

        if (update_authority == g_nifty_program_authority_address) {
            new_entry_addresses.add(get_entry_address(mint_address));
        }
    }

    async fetch_admin_address()
    {
        let result = await this.cluster.rpc_connections.run((rpc_connection) =>
            {
                return rpc_connection.getAccountInfo(g_config_address,
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

    async stake_entry(entry, stake_account, sign_callback)
    {
        return this.complete_tx((wallet_address) => {
            return this.make_stake_tx(entry, stake_account, wallet_address);
        }, sign_callback);
    }
    
    async destake_entry(entry, sign_callback)
    {
        return this.complete_tx((wallet_address) => {
            return this.make_destake_tx(entry, wallet_address);
        }, sign_callback);
    }
    
    async harvest_entry(entry, sign_callback)
    {
        return this.complete_tx((wallet_address) => {
            return this.make_harvest_tx(entry, wallet_address);
        }, sign_callback);
    }
    
    async level_up_entry(entry, sign_callback)
    {
        return this.complete_tx((wallet_address) => {
            return this.make_level_up_tx(entry, wallet_address);
        }, sign_callback);
    }
    
    // Private implementation follows ---------------------------------------------------------------------------------

    async make_buy_tx(entry, maximum_price_lamports, wallet_address)
    {
        let admin_address = await this.fetch_admin_address();

        let token_destination_address = get_associated_token_address(wallet_address, entry.mint_address);

        return _buy_tx({ funding_pubkey : wallet_address,
                         config_pubkey : g_config_address,
                         admin_pubkey : admin_address,
                         block_pubkey : entry.block.address,
                         whitelist_pubkey : get_whitelist_address(entry.block.address),
                         entry_pubkey : entry.address,
                         entry_token_pubkey : entry.token_address,
                         entry_mint_pubkey : entry.mint_address,
                         token_destination_pubkey : token_destination_address,
                         token_destination_owner_pubkey : wallet_address,
                         metaplex_metadata_pubkey : entry.metaplex_metadata_address,
                         maximum_price_lamports : maximum_price_lamports });
    }
    
    async make_refund_tx(entry, wallet_address)
    {
        return _refund_tx({ token_owner_pubkey : wallet_address,
                            block_pubkey : entry.block.address,
                            entry_pubkey : entry.address,
                            buyer_token_pubkey : get_associated_token_address(wallet_address, entry.mint_address),
                            refund_destination_pubkey : wallet_address });
    }
   
    async make_bid_tx(entry, minimum_bid_lamports, maximum_bid_lamports, wallet_address)
    {
        let bid_marker_token_address = get_bid_marker_token_address(entry.mint_address, wallet_address);
           
        return _bid_tx({ bidding_pubkey : wallet_address,
                         entry_pubkey : entry.address,
                         bid_marker_token_pubkey : bid_marker_token_address,
                         bid_pubkey : get_bid_address(bid_marker_token_address),
                         minimum_bid_lamports : minimum_bid_lamports,
                         maximum_bid_lamports : maximum_bid_lamports });
    }
    
    async make_claim_tx(entry, won, wallet_address)
    {
        let bid_marker_token_address = get_bid_marker_token_address(entry.mint_address, wallet_address);
        let bid_address = get_bid_address(bid_marker_token_address);

        if (won) {
            let admin_address = await this.fetch_admin_address();
       
            let token_destination_address = get_associated_token_address(wallet_address, entry.mint_address);
            let bid_marker_token_address = get_bid_marker_token_address(entry.mint_address, wallet_address);
       
            return _claim_winning_tx({ bidding_pubkey : wallet_address,
                                       entry_pubkey : entry.address,
                                       bid_pubkey : bid_address,
                                       config_pubkey : g_config_address,
                                       admin_pubkey : admin_address,
                                       entry_token_pubkey : entry.token_address,
                                       entry_mint_pubkey : entry.mint_address,
                                       token_destination_pubkey : token_destination_address,
                                       token_destination_owner_pubkey : wallet_address,
                                       bid_marker_token_pubkey : bid_marker_token_address });
        }
        else {
            return _claim_losing_tx({ bidding_pubkey : wallet_address,
                                      entry_pubkey : entry.address,
                                      bid_pubkey : bid_address,
                                      bid_marker_token_pubkey : bid_marker_token_address });
        }
    }
    
    async make_stake_tx(entry, stake_address, wallet_address)
    {
        return _stake_tx({ block_pubkey : entry.block.address,
                           entry_pubkey : entry.address,
                           token_owner_pubkey : wallet_address,
                           token_pubkey : get_associated_token_address(wallet_address, entry.mint_address),
                           stake_pubkey : stake_address,
                           withdraw_authority : wallet_address });
    }
    
    async make_destake_tx(entry, wallet_address)
    {
       return _destake_tx({ funding_pubkey : wallet_address,
                            block_pubkey : entry.block.address,
                            entry_pubkey : entry.address,
                            token_owner_pubkey : wallet_address,
                            token_pubkey : get_associated_token_address(wallet_address, entry.mint_address),
                            stake_pubkey : entry.owned_stake_account,
                            ki_destination_pubkey : get_associated_token_address(wallet_address, g_ki_mint_address),
                            ki_destination_owner_pubkey : wallet_address,
                            bridge_pubkey : get_entry_bridge_address(entry.mint_address),
                            new_withdraw_authority : wallet_address });
    }
    
    async make_harvest_tx(entry, wallet_address)
    {
        return _harvest_tx({ funding_pubkey : wallet_address,
                             entry_pubkey : entry.address,
                             token_owner_pubkey : wallet_address,
                             token_pubkey : get_associated_token_address(wallet_address, entry.mint_address),
                             stake_pubkey : entry.owned_stake_account,
                             ki_destination_pubkey : get_associated_token_address(wallet_address, g_ki_mint_address),
                             ki_destination_owner_pubkey : wallet_address });
    }
    
    async make_level_up_tx(entry, wallet_address)
    {
        return _level_up_tx({ entry_pubkey : entry.address,
                              token_owner_pubkey : wallet_address,
                              token_pubkey : get_associated_token_address(wallet_address, entry.mint_address),
                              entry_metaplex_metadata_pubkey : entry.metaplex_metadata_address,
                              ki_source_pubkey : get_associated_token_address(wallet_address, g_ki_mint_address),
                              ki_source_owner_pubkey : wallet_address });
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

//   destake_tx()
//   {
//       let wallet_address = this.block.cluster.wallet_address;
//       if (wallet_address == null) {
//           return null;
//       }
//       
//   }
//   
//   take_commission_or_delegate_tx()
//   {
//       let wallet_address = this.block.cluster.wallet_address;
//       if (wallet_address == null) {
//           return null;
//       }
//       
//       return _take_commission_or_delegate_tx({ funding_address : wallet_address,
//                                                block_address : this.block.address,
//                                                entry_address : this.address,
//                                                stake_address : this.owned_stake_account,
//                                                bridge_address : get_entry_bridge_address(this.mint_address) });
//   }
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
    return find_pda([ [ 7 ],
                      u32_to_le_bytes(group_number),
                      u32_to_le_bytes(block_number) ],
                    g_nifty_program_pubkey)[0].toBase58();
}


function get_whitelist_address(block_address)
{
    return find_pda([ [ 13 ],
                      address_to_buffer(block_address) ],
                    g_nifty_program_pubkey)[0].toBase58();
}


function get_entry_mint_address(block_address, entry_index)
{
    return find_pda([ [ 5 ],
                      address_to_buffer(block_address),
                      u16_to_le_bytes(entry_index) ],
                    g_nifty_program_pubkey)[0].toBase58();
}


function get_entry_address(entry_mint_address)
{
    return find_pda([ [ 8 ],
                      address_to_buffer(entry_mint_address) ],
                    g_nifty_program_pubkey)[0].toBase58();
}


function get_entry_bridge_address(entry_mint_address)
{
    return find_pda([ [ 10 ],
                      address_to_buffer(entry_mint_address) ],
                    g_nifty_program_pubkey)[0].toBase58();
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
    return find_pda([ [ 12 ],
                      address_to_buffer(entry_mint_address),
                      address_to_buffer(bidder_address) ],
                    g_nifty_program_pubkey)[0].toBase58();
}


function get_bid_address(bid_marker_token_address)
{
    return find_pda([ [ 9 ],
                      address_to_buffer(bid_marker_token_address) ],
                    g_nifty_program_pubkey)[0].toBase58();
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


function buffer_le_u32(buffer, offset)
{
    return (((buffer[offset + 0] & 0xFF) <<  0) |
            ((buffer[offset + 1] & 0xFF) <<  8) |
            ((buffer[offset + 2] & 0xFF) << 16) |
            ((buffer[offset + 3] & 0xFF) << 32));
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


function u32_to_le_bytes(u)
{
    return [ (u >>  0) & 0xFF,
             (u >>  8) & 0xFF,
             (u >> 16) & 0xFF,
             (u >> 23) & 0xFF ];
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
