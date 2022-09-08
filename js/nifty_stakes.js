'use strict';

const SolanaWeb3 = require("@solana/web3.js");
const Buffer = require("buffer");
const bs58 = require("bs58");
const { _buy_tx,
        _buy_mystery_tx,
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

const NIFTY_PROGRAM_ADDRESS = "ShinboVZNAn1UjpZ3rJsFzLcWMP5JF8LPdHPWaaGYTV";
const METAPLEX_PROGRAM_ADDRESS = "metaqbxxUerdq28cj1RbAWkYQm3ybzjb6a8bt518x1s";
const SPL_TOKEN_PROGRAM_ADDRESS = "TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA";
const SPL_ASSOCIATED_TOKEN_PROGRAM_ADDRESS = "ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL";
const STAKE_PROGRAM_ADDRESS = "Stake11111111111111111111111111111111111111";

const BLOCKS_AT_ONCE = 3;
const ENTRIES_AT_ONCE = 20;

const g_nifty_program_pubkey = new SolanaWeb3.PublicKey(NIFTY_PROGRAM_ADDRESS);
const g_metaplex_program_pubkey = new SolanaWeb3.PublicKey(METAPLEX_PROGRAM_ADDRESS);
const g_spl_token_program_pubkey = new SolanaWeb3.PublicKey(SPL_TOKEN_PROGRAM_ADDRESS);
const g_spl_associated_token_program_pubkey = new SolanaWeb3.PublicKey(SPL_ASSOCIATED_TOKEN_PROGRAM_ADDRESS);
const g_stake_program_pubkey = new SolanaWeb3.PublicKey(STAKE_PROGRAM_ADDRESS);

const g_nifty_program_authority_pubkey =
      SolanaWeb3.PublicKey.findProgramAddressSync([ [ 2 ] ], g_nifty_program_pubkey)[0];
      
const g_config_pubkey = SolanaWeb3.PublicKey.findProgramAddressSync([ [ 1 ] ], g_nifty_program_pubkey)[0];

const g_master_stake_pubkey = SolanaWeb3.PublicKey.findProgramAddressSync([ [ 3 ] ], g_nifty_program_pubkey)[0];

const g_ki_mint_pubkey = SolanaWeb3.PublicKey.findProgramAddressSync([ [ 4 ] ], g_nifty_program_pubkey)[0];

const g_ki_metadata_pubkey = metaplex_metadata_pubkey(g_ki_mint_pubkey);

const g_bid_marker_mint_pubkey = SolanaWeb3.PublicKey.findProgramAddressSync([ [ 11 ] ], g_nifty_program_pubkey)[0];


function delay(delay_ms)
{
    return new Promise((resolve) => setTimeout(resolve, delay_ms));
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

    async getAccountInfo(pubkey, config)
    {
        return this.execute(() => { return this.connection.getAccountInfo(pubkey, config); }, 10 * 1024);
    }

    async getEpochInfo()
    {
        return this.execute(() => { return this.connection.getEpochInfo(); }, 1024);
    }

    async getBlockTime(absoluteSlot)
    {
        return this.execute(() => { return this.connection.getBlockTime(absoluteSlot); }, 1024);
    }

    async getMultipleAccountsInfo(block_pubkeys)
    {
        return this.execute(() => { return this.connection.getMultipleAccountsInfo(block_pubkeys); },
                            block_pubkeys.length * 10 * 1024);
    }

    async getBalance(wallet_pubkey)
    {
        return this.execute(() => { return this.connection.getBalance(wallet_pubkey); }, 1024);
    }
    
    async getParsedTokenAccountsByOwner(wallet_pubkey, config)
    {
        return this.execute(() => { return this.connection.getParsedTokenAccountsByOwner(wallet_pubkey, config); },
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
            if (this.requests_array[i].until > now) {
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
    // an error, retries it after 1 second.  If the RpcConnections is shut down, throws an error.
    // If the request cannot be run because the connection is overloaded, throws an error.
    async run(func, error_prefix)
    {
        let rpc_connection = this.get();

        while (true) {
            try {
                return await func(this.get());
            }
            catch (error) {
                if (this.is_shutdown) {
                    throw new Error("shutdown");
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
    // If default_slot_duration_seconds is null, a default is used.
    static create(rpc_connections, default_slot_duration_seconds, callbacks)
    {
        return new Cluster(rpc_connections, default_slot_duration_seconds, callbacks);
    }

    constructor(rpc_connections, default_slot_duration_seconds, callbacks)
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

        this.default_slot_duration_seconds = default_slot_duration_seconds;
        
        this.callbacks = callbacks;

        // Map from block pubkey to block
        this.blocks = new Map();

        // Map from entry pubkey to entry
        this.entries = new Map();

        // In-order array of entry pubkeys
        this.entry_pubkeys = [ ];

        // Each query for an entry increments an index.  This allows background queries for entries to ignore their
        // results if a refresh occurred since the background query started.
        this.entry_query_index = new Map();

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
     * Returns { confirmed_epoch, confirmed_slot, confirmed_unix_timestamp,
     *           slot, unix_timestamp }
     *
     * or null if there is no available clock yet
     *
     * If slot_duration_seconds is provided it is used, otherwise the value provided to the constructor is
     * used.
     *
     * confirmed_epoch is the epoch at the most recently confirmed slot known to the client.
     * confirmed_slot is the most recently confirmed slot known to the client.
     * confirmed_unix_timestamp is the cluster timestamp of the most recently confirmed slot known to the client.
     * slot is an estimation of the current confirmed slot.
     * unix_timestamp is an estimation of the cluster timestamp of slot.
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

        return {
            confirmed_epoch : this.confirmed_epoch,
            confirmed_slot : this.confirmed_slot,
            confirmed_unix_timestamp : this.confirmed_unix_timestamp,
            slot : this.confirmed_slot + slots_elapsed,
            unix_timestamp : this.confirmed_unix_timestamp + (((now - this.confirmed_query_time) / 1000) | 0)
        };
    }

    // Get the entry count
    entry_count()
    {
        return this.entry_pubkeys.length;
    }

    // Get the entry at a given index
    entry_at(index)
    {
        return this.entries.get(this.entry_pubkeys[index]);
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

    // Causes an entry to be immediately refreshed.  If this causes the Entry to change, asynchronously a callback
    // into the callbacks' on_entry_changed function will be made.
    async refresh_entry(entry)
    {
        let block_result = await this.rpc_connections.run((rpc_connection) => {
            return rpc_connection.getAccountInfo(entry.block.pubkey);
        }, "refresh entry update block");

        let entry_result = await this.rpc_connections.run((rpc_connection) => {
            return rpc_connection.getAccountInfo(entry.pubkey);
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
        let block_pubkeys = [ ];
        
        for (let i = 0; i < BLOCKS_AT_ONCE; i++) {
            block_pubkeys.push(get_block_pubkey(group_number, block_number + i));
        }

        let results = await this.rpc_connections.run((rpc_connection) =>
            {
                return rpc_connection.getMultipleAccountsInfo(block_pubkeys);
            }, "update blocks get accounts");
        
        let entries_promises = [ ];

        let idx;
        for (idx = 0; idx < results.length; idx += 1) {
            if (results[idx] == null) {
                break;
            }
            let block_pubkey = block_pubkeys[idx];
            let block_pubkey_string = block_pubkey.toBase58();
            let block = this.blocks.get(block_pubkey_string);
            let block_changed;
            if (block == null) {
                block = new Block(this, block_pubkeys[idx], results[idx].data);
                // Ignore blocks that are not complete
                if (block.added_entries_count < block.total_entry_count) {
                    continue;
                }
                this.blocks.set(block_pubkey_string, block);
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
        let entry_pubkeys = [ ];
        
        for (let i = 0; i < ENTRIES_AT_ONCE; i++) {
            entry_pubkeys.push(get_entry_pubkey(get_entry_mint_pubkey(block.pubkey, entry_index + i)));
        }
        
        let results = await this.rpc_connections.run((rpc_connection) =>
            {
                return rpc_connection.getMultipleAccountsInfo(entry_pubkeys);
            }, "update entries query accounts");

        let idx;
        for (idx = 0; idx < results.length; idx += 1) {
            if (results[idx] == null) {
                break;
            }
            let entry_pubkey = entry_pubkeys[idx];
            let entry_pubkey_string = entry_pubkey.toBase58();
            let entry = this.entries.get(entry_pubkey_string);
            if (entry == null) {
                entry = new Entry(block, entry_pubkeys[idx], results[idx].data);
                this.entries.set(entry_pubkey_string, entry);
                this.entry_pubkeys.push(entry_pubkey);
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
}


// All pubkeys are SolanaWeb3.PublicKey objects.  To display them, use .toBase58().
class Block
{
    constructor(entries, pubkey, data)
    {
        this.entries = entries;
        this.pubkey = pubkey;
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
        this.added_entries_count = buffer_le_u16(data, 64);
        this.block_start_timestamp = Number(buffer_le_s64(data, 72));
        this.mysteries_sold_count = buffer_le_u16(data, 80);
        this.mystery_phase_end_timestamp = Number(buffer_le_s64(data, 88));
        this.commission = buffer_le_u16(data, 96);
        this.last_commission_change_epoch = Number(buffer_le_u64(data, 104));
    }

    update(data)
    {
        let new_block = new Block(this.entries, this.pubkey, data);

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
        
        let mystery_phase_end = this.block_start_timestamp + this.mystery_phase_duration;

        return (clock.unix_timestamp > mystery_phase_end);
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


// All pubkeys are SolanaWeb3.PublicKey objects.  To display them, use .toBase58().
class Entry
{
    constructor(block, pubkey, data)
    {
        this.block = block;
        this.pubkey = pubkey;
        this.group_number = buffer_le_u32(data, 36);
        this.block_number = buffer_le_u32(data, 40);
        this.entry_index = buffer_le_u16(data, 44);
        this.mint_pubkey = buffer_pubkey(data, 46);
        this.token_pubkey = buffer_pubkey(data, 78);
        this.metaplex_metadata_pubkey = buffer_pubkey(data, 110);
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
        this.auction_winning_bid_pubkey = buffer_pubkey(data, 232);
        this.owned_stake_account = buffer_pubkey(data, 264);
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
                form : data[400 + (i * 272)],
                skill : data[401 + (i * 272)],
                ki_factor : buffer_le_u32(data, 404 + (i * 272)),
                name : buffer_string(data, 408 + (i * 272), 32),
                uri : buffer_string(data, 440 + (i * 272), 200),
                uri_contents_sha256 : buffer_sha256(data, 640 + (i * 272))
            };
        }
    }

    update(data)
    {
        let new_entry = new Entry(this.block, this.pubkey, data);

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
        
        if (!new_entry.auction_winning_bid_pubkey.equals(this.auction_winning_bid_pubkey)) {
            this.auction_winning_bid_pubkey = new_entry.auction_winning_bid_pubkey;
            changed = true;
        }
        
        if (!new_entry.owned_stake_account.equals(this.owned_stake_account)) {
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

        return changed;
    }

    // Returns the metaplex metadata URI for the entry, as stored in the metaplex metadata of the entry
    async get_metaplex_metadata_uri(rpc_connections)
    {
        let result = await rpc_connections.run((rpc_connection) => {
            return rpc_connection.getAccountInfo(this.metaplex_metadata_pubkey);
        }, "fetch metaplex metadata");

        let data = result.data;

        // Skip key, update_authority, mint
        let offset = 1 + 32 + 32;
        // name_len
        let len = buffer_le_u32(data, offset);
        if (len > 200) {
            // Bogus data
            throw new Error("Invalid metaplex metadata for " + this.metaplex_metadata_pubkey.toBase58());
        }
        // Skip name
        offset += 4 + len;
        // Symbol, string of at most 4 characters
        len = buffer_le_u32(data, offset);
        if (len > 10) {
            throw new Error("Invalid metaplex metadata for " + this.metaplex_metadata_pubkey.toBase58());
        }
        // Skip symbol;
        offset += 4 + len;
        // Uri, string of at most 200 characters
        len = buffer_le_u32(data, offset);
        if (len > 200) {
            throw new Error("Invalid metaplex metadata for " + this.metaplex_metadata_pubkey.toBase58());
        }
        
        return buffer_string(data, offset + 4, len);
    }

    // Cribbed from the nifty program's get_entry_state() function
    get_entry_state(clock)
    {
        if (this.reveal_sha256 == "0000000000000000000000000000000000000000000000000000000000000000") {
            if (this.purchase_price_lamports > 0) {
                if (this.owned_stake_account != null) {
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
// All pubkeys passed into and returned from Wallet are strings.
class Wallet
{
    static create(rpc_connections, wallet_pubkey)
    {
        return new Wallet(rpc_connections, wallet_pubkey, callbacks);
    }

    // wallet_pubkey is a base-58 string
    constructor(rpc_connections, wallet_pubkey, callbacks)
    {
        this.rpc_connections = rpc_connections;

        this.wallet_pubkey_holder = { wallet_pubkey : null };
        
        this.set_wallet_pubkey(wallet_pubkey);

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

    set_wallet_pubkey(new_wallet_pubkey)
    {
        if (new_wallet_pubkey == null) {
            if (this.wallet_pubkey_holder.wallet_pubkey == null) {
                return;
            }
        }
        else {
            new_wallet_pubkey = new SolanaWeb3.PublicKey(new_wallet_pubkey);

            if ((this.wallet_pubkey_holder.wallet_pubkey != null) &&
                (this.wallet_pubkey_holder.wallet_pubkey.equals(new_wallet_pubkey))) {
                return;
            }
        }

        this.wallet_pubkey_holder = { wallet_pubkey : new_wallet_pubkey };

        // Reset saved state because the wallet pubkey has changed
        this.last_balance_fetch_time = null;
        this.sol_balance = null;
        this.ki_balance = null;
        this.entry_pubkeys = new Set();
        this.bids_by_entry_pubkey = new Map();
        this.bids_by_entry_mint = new Map();
        this.stakes = [ ];
        this.last_tokens_fetch_time = null;
        this.last_stakes_fetch_time = null;
    }

    // Returns a string, or null
    get_wallet_pubkey()
    {
        if (this.wallet_pubkey_holder.wallet_pubkey == null) {
            return null;
        }

        return this.wallet_pubkey_holder.wallet_pubkey.toBase58();
    }
    
    async get_sol_balance()
    {
        // Save wallet_pubkey_holder so that we know if it changed
        let wallet_pubkey_holder = this.wallet_pubkey_holder;
        let wallet_pubkey = this.wallet_pubkey_holder.wallet_pubkey;
        
        // If the wallet has been "shut down", don't query
        if (wallet_pubkey == null) {
            return null;
        }

        let now = Date.now();

        // If never fetched balance, fetch it
        if ((this.last_balance_fetch_time == null) ||
            // If haven't fetched balance for 5 seconds, fetch it
            ((now - this.last_balance_fetch_time) > 5000)) {

            // Wait on getting SOL balance of wallet
            let sol_balance = await this.rpc_connections.run((rpc_connection) => {
                return rpc_connection.getBalance(wallet_pubkey);
            }, "update sol balance");
            
            if (wallet_pubkey_holder == this.wallet_pubkey_holder) {
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

    // Returns the string pubkeys of entries owned by this 
    async get_entry_pubkeys()
    {
        await this.update_token_data();

        return this.entry_pubkeys.values();
    }

    // Returns an interator over { entry_pubkey, lamports }.  lamports is the number of lamports in the bid account,
    // which could be more than the bid if lamports were added post-bid.
    async get_bids()
    {
        await this.update_token_data();

        return this.bids_by_entry_pubkey.values();
    }

    async get_stakes()
    {
        await this.update_stakes();

        return this.stakes;
    }

    // Returns true if the wallet owns the entry with the given string entry pubkey, false if not
    async owns_entry(entry_pubkey)
    {
        await this.update_token_data();

        return this.entry_pubkeys.has(entry_pubkey);
    }

    // Returns undefined if the wallet has no bid for the string entry pubkey, or the lamports of the bid if it does
    async get_bid(entry_pubkey)
    {
        await this.update_token_data();

        return this.bids_by_entry_mint.get(entry.mint_pubkey.toBase58());
    }
    
    async update_token_data()
    {
        // Save wallet_pubkey_holder so that we know if it changed
        let wallet_pubkey_holder = this.wallet_pubkey_holder;
        let wallet_pubkey = this.wallet_pubkey_holder.wallet_pubkey;
        
        // If the wallet has been "shut down", don't query
        if (wallet_pubkey == null) {
            return null;
        }

        let now = Date.now();

        // If never fetched balance, fetch it
        if ((this.last_tokens_fetch_time == null) ||
            // If haven't fetched tokens in 30 seconds, fetch them
            ((now - this.last_tokens_fetch_time) > 30000)) {
            let results = await this.rpc_connections.run((rpc_connection) =>
                {
                    return rpc_connection.getParsedTokenAccountsByOwner(wallet_pubkey,
                                                                        { programId : g_spl_token_program_pubkey });
                }, "update token data token accounts");

            let new_ki_balance = 0;

            let new_entry_pubkeys = new Set();

            let new_bids_by_entry_pubkey = new Map();

            let new_bids_by_entry_mint = new Map();
            
            let promises = [ ];

            for (let idx = 0; idx < results.value.length; idx++) {
                let account = results.value[idx].account;
                let pubkey = results.value[idx].pubkey;
                
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
                if (account.data.parsed.info.owner != wallet_pubkey.toBase58()) {
                    continue;
                }

                let mint_pubkey = new SolanaWeb3.PublicKey(account.data.parsed.info.mint);

                // If its mint is the bid mint, then it's a bid token, so add an async function to fetch the
                // details of the bid and add it to new_bids_by_entry_pubkey
                if (mint_pubkey.equals(g_bid_marker_mint_pubkey)) {
                    pubkey = new SolanaWeb3.PublicKey(pubkey);
                    promises.push(this.process_bid(pubkey, new_bids_by_entry_pubkey, new_bids_by_entry_mint));
                }
                // Else if its mint is the ki mint, then add its ki tokens to the total
                else if (mint_pubkey.equals(g_ki_mint_pubkey)) {
                    new_ki_balance += account.data.parsed.info.tokenAmount.amount;
                }
                else {
                    // Add an async function to check to see if its update authority is the Shinobi Immortals program
                    // authority, and if so, add it to new_entries.
                    promises.push(this.process_mint(mint_pubkey, new_entry_pubkeys));
                }
            }

            await Promise.all(promises);

            // Now rebuild this.ki_balance, this.entry_pubkeys, this.bids_by_entry_pubkey, and this.bids_by_entry_mint
            // from the resulting data
            if (wallet_pubkey_holder == this.wallet_pubkey_holder) {
                this.ki_balance = new_ki_balance;
                this.entry_pubkeys = new_entry_pubkeys;
                this.bids_by_entry_pubkey = new_bids_by_entry_pubkey;
                this.bids_by_entry_mint = new_bids_by_entry_mint;
                
                this.last_tokens_fetch_time = Date.now();
            }
        }
    }

    async process_bid(bid_marker_token_pubkey, bids_by_entry_pubkey, bids_by_entry_mint)
    {
        let result = this.rpc_connections.run((rpc_connection) =>
            {
                return rpc_connection.getAccountInfo(get_bid_pubkey(bid_marker_token_pubkey),
                                                     {
                                                         dataSlice : { offset : 4,
                                                                       length : 32 }
                                                     });
            }, "fetch bid account");

        let entry_mint_pubkey = new SolanaWeb3.PublicKey(result.data);

        let entry_pubkey = get_entry_pubkey(entry_mint_pubkey).toBase58();

        let value = { entry_pubkey : entry_pubkey, lamports : result.lamports };

        bids_by_entry_pubkey.set(entry_pubkey, value);
        
        bids_by_entry_mint.set(entry_mint_pubkey.toBase58(), value);
    }

    async process_mint(mint_pubkey, new_entry_pubkeys)
    {
        // Load the metaplex metadata
        let result = await this.rpc_connections.run((rpc_connection) => {
            return rpc_connection.getAccountInfo(metaplex_metadata_pubkey(mint_pubkey));
        }, "fetch metaplex metadata");

        if (result == null) {
            // No metadata at all
            return;
        }

        let update_authority = buffer_pubkey(result.data, 1);

        if (update_authority.equals(g_nifty_program_authority_pubkey)) {
            new_entry_pubkeys.add(get_entry_pubkey(mint_pubkey).toBase58());
        }
    }

    async fetch_admin_pubkey()
    {
        let result = await this.rpc_connections.run((rpc_connection) =>
            {
                return rpc_connection.getAccountInfo(g_config_pubkey,
                                                     {
                                                         dataSlice : { offset : 4,
                                                                       length : 32 }
                                                     });
            }, "fetch admin pubkey");
        
        if (result == null) {
            return null;
        }
        
        return new SolanaWeb3.PublicKey(result.data);
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
    
    // maximum_price_lamports should be the price that was shown to the user, but null can be passed in and it will
    // be fetched from current data.
    // Returns the string transaction id of the completed transaction.  Throws an error on all failures.
    async buy_entry(entry, clock, maximum_price_lamports, mystery, sign_callback)
    {
        return this.complete_tx((wallet_pubkey) => {
            return this.make_buy_tx(entry, clock, maximum_price_lamports, mystery, wallet_pubkey);
        }, sign_callback);
    }

    async make_buy_tx(entry, clock, maximum_price_lamports, mystery, wallet_pubkey)
    {
        if (maximum_price_lamports == null) {
            maximum_price_lamports = entry.get_price(clock);
        }
        
        let admin_pubkey = await this.fetch_admin_pubkey();

        let token_destination_pubkey = get_associated_token_pubkey(wallet_pubkey, entry.mint_pubkey);

        return mystery ?
            _buy_mystery_tx({ funding_pubkey : wallet_pubkey,
                              config_pubkey : g_config_pubkey,
                              admin_pubkey : admin_pubkey,
                              block_pubkey : entry.block.pubkey,
                              entry_pubkey : entry.pubkey,
                              entry_token_pubkey : entry.token_pubkey,
                              entry_mint_pubkey : entry.mint_pubkey,
                              token_destination_pubkey : token_destination_pubkey,
                              token_destination_owner_pubkey : wallet_pubkey,
                              metaplex_metadata_pubkey : entry.metaplex_metadata_pubkey,
                              maximum_price_lamports : maximum_price_lamports }) :
            _buy_tx({ funding_pubkey : wallet_pubkey,
                      config_pubkey : g_config_pubkey,
                      admin_pubkey : admin_pubkey,
                      block_pubkey : entry.block.pubkey,
                      entry_pubkey : entry.pubkey,
                      entry_token_pubkey : entry.token_pubkey,
                      entry_mint_pubkey : entry.mint_pubkey,
                      token_destination_pubkey : token_destination_pubkey,
                      token_destination_owner_pubkey : wallet_pubkey,
                      metaplex_metadata_pubkey : entry.metaplex_metadata_pubkey,
                      maximum_price_lamports : maximum_price_lamports });
    }

    
    async complete_tx(tx_maker_func, sign_callback)
    {
        let wallet_pubkey_holder = this.wallet_pubkey_holder;
        let wallet_pubkey = wallet_pubkey_holder.wallet_pubkey;
        if (wallet_pubkey == null) {
            throw new Error("No wallet");
        }

        while (true) {
            let tx = await tx_maker_func(wallet_pubkey);

            if (wallet_pubkey_holder != this.wallet_pubkey_holder) {
                // Wallet pubkey changed, so abort
                throw new Error("Wallet change");
            }
            
            let recent_blockhash_result = await this.rpc_connections.run((rpc_connection) =>
                {
                    return rpc_connection.getLatestBlockhash();
                }, "fetch recent blockhash");

            if (wallet_pubkey_holder != this.wallet_pubkey_holder) {
                // Wallet pubkey changed, so abort
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

            if (wallet_pubkey_holder != this.wallet_pubkey_holder) {
                // Wallet pubkey changed, so abort
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
                return await this.rpc_connections.run_once((rpc_connection) => {
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

//   // maximum_price_lamports should be the price that was shown to the user
//   async buy_mystery(entry, clock, maximum_price_lamports)
//   {
//       // Get the current price of the entry; the user is willing to pay this much.  This maximum is guaranteed to be
//       // no higher than any value that was presented to them (since clock only moves forwards and thus entry price
//       // can only go down).
//       let maximum_price_lamports = this.get_price(this.block.cluster.get_clock());
//       
//       let wallet_pubkey = this.block.cluster.wallet_pubkey;
//       if (wallet_pubkey == null) {
//           return;
//       }
//
//       let admin_pubkey = await this.block.cluster.admin_pubkey(g_config_pubkey);
//       if (this.is_shutdown || (wallet_pubkey != this.block.cluster.wallet_pubkey)) {
//           return;
//       }
//
//       let token_destination_pubkey = get_associated_token_pubkey(wallet_pubkey, this.mint_pubkey);
//
//       return _buy_mystery_tx({ funding_pubkey : wallet_pubkey,
//                                config_pubkey : g_config_pubkey,
//                                admin_pubkey : admin_pubkey,
//                                block_pubkey : this.block.pubkey,
//                                entry_pubkey : this.pubkey,
//                                entry_token_pubkey : this.token_pubkey,
//                                entry_mint_pubkey : this.mint_pubkey,
//                                token_destination_pubkey : token_destination_pubkey,
//                                token_destination_owner_pubkey : wallet_pubkey,
//                                metaplex_metadata_pubkey : this.metaplex_metadata_pubkey,
//                                maximum_price_lamports : maximum_price_lamports });
//   }
//   
//   refund_tx()
//   {
//       let wallet_pubkey = this.block.cluster.wallet_pubkey;
//       if (wallet_pubkey == null) {
//           return null;
//       }
//       
//       return _refund_tx({ token_owner_pubkey : wallet_pubkey,
//                           block_pubkey : this.block.pubkey,
//                           entry_pubkey : this.pubkey,
//                           buyer_token_pubkey : get_associated_token_pubkey(wallet_pubkey, this.mint_pubkey),
//                           refund_destination_pubkey : wallet_pubkey });
//   }
//   
//   bid_tx(minimum_bid_lamports, maximum_bid_lamports)
//   {
//       let wallet_pubkey = this.block.cluster.wallet_pubkey;
//       if ((wallet_pubkey == null) || (minimum_bid_lamports == null) || (maximum_bid_lamports == null)) {
//           return null;
//       }
//       let bid_marker_token_pubkey = get_bid_marker_token_pubkey(this.mint_pubkey, wallet_pubkey);
//           
//       return _bid_tx({ bidding_pubkey : wallet_pubkey,
//                        entry_pubkey : this.pubkey,
//                        bid_marker_token_pubkey : bid_marker_token_pubkey,
//                        bid_pubkey : get_bid_pubkey(bid_marker_token_pubkey),
//                        minimum_bid_lamports : minimum_bid_lamports,
//                        maximum_bid_lamports : maximum_bid_lamports });
//   }
//   
//   claim_losing_tx()
//   {
//       let wallet_pubkey = this.block.cluster.wallet_pubkey;
//       if (wallet_pubkey == null) {
//           return null;
//       }
//       let bid_marker_token_pubkey = get_bid_marker_token_pubkey(this.mint_pubkey, wallet_pubkey);
//           
//       return _claim_losing_tx({ bidding_pubkey : wallet_pubkey,
//                                 entry_pubkey : this.pubkey,
//                                 bid_pubkey : get_bid_pubkey(bid_marker_token_pubkey),
//                                 bid_marker_token_pubkey : bid_marker_token_pubkey });
//   }
//   
//   async claim_winning_tx()
//   {
//       let wallet_pubkey = this.block.cluster.wallet_pubkey;
//       if (wallet_pubkey == null) {
//           return null;
//       }
//
//       let admin_pubkey = await this.block.cluster.admin_pubkey(g_config_pubkey);
//       if (this.is_shutdown || (wallet_pubkey != this.block.cluster.wallet_pubkey)) {
//           return;
//       }
//       
//       let token_destination_pubkey = get_associated_token_pubkey(wallet_pubkey, this.mint_pubkey);
//       let bid_marker_token_pubkey = get_bid_marker_token_pubkey(this.mint_pubkey, wallet_pubkey);
//       
//       return _claim_winning_tx({ bidding_pubkey : wallet_pubkey,
//                                  entry_pubkey : this.pubkey,
//                                  bid_pubkey : get_bid_pubkey(bid_marker_token_pubkey),
//                                  config_pubkey : g_config_pubkey,
//                                  admin_pubkey : admin_pubkey,
//                                  entry_token_pubkey : this.token_pubkey,
//                                  entry_mint_pubkey : this.mint_pubkey,
//                                  token_destination_pubkey : token_destination_pubkey,
//                                  token_destination_owner_pubkey : wallet_pubkey,
//                                  bid_marker_token_pubkey : bid_marker_token_pubkey });
//   }
//   
//   stake_tx(stake_account_pubkey)
//   {
//       let wallet_pubkey = this.block.cluster.wallet_pubkey;
//       if ((wallet_pubkey == null) || (stake_account_pubkey == null)) {
//           return null;
//       }
//       
//       return _stake_tx({ block_pubkey : this.block.pubkey,
//                          entry_pubkey : this.pubkey,
//                          token_owner_pubkey : wallet_pubkey,
//                          token_pubkey : get_associated_token_pubkey(wallet_pubkey, this.mint_pubkey),
//                          stake_pubkey : stake_account_pubkey,
//                          withdraw_authority : wallet_pubkey });
//   }
//   
//   destake_tx()
//   {
//       let wallet_pubkey = this.block.cluster.wallet_pubkey;
//       if (wallet_pubkey == null) {
//           return null;
//       }
//       
//       return _destake_tx({ funding_pubkey : wallet_pubkey,
//                            block_pubkey : this.block.pubkey,
//                            entry_pubkey : this.pubkey,
//                            token_owner_pubkey : wallet_pubkey,
//                            token_pubkey : get_associated_token_pubkey(wallet_pubkey, this.mint_pubkey),
//                            stake_pubkey : this.owned_stake_account,
//                            ki_destination_pubkey : get_associated_token_pubkey(wallet_pubkey, g_ki_mint_pubkey),
//                            ki_destination_owner_pubkey : wallet_pubkey,
//                            bridge_pubkey : get_entry_bridge_pubkey(this.mint_pubkey),
//                            new_withdraw_authority : wallet_pubkey.toBase58() });
//   }
//   
//   harvest_tx()
//   {
//       let wallet_pubkey = this.block.cluster.wallet_pubkey;
//       if (wallet_pubkey == null) {
//           return null;
//       }
//       
//       return _harvest_tx({ funding_pubkey : wallet_pubkey,
//                            entry_pubkey : this.pubkey,
//                            token_owner_pubkey : wallet_pubkey,
//                            token_pubkey : get_associated_token_pubkey(wallet_pubkey, this.mint_pubkey),
//                            stake_pubkey : this.owned_stake_account,
//                            ki_destination_pubkey : get_associated_token_pubkey(wallet_pubkey, g_ki_mint_pubkey),
//                            ki_destination_owner_pubkey : wallet_pubkey });
//   }
//   
//   level_up_tx()
//   {
//       let wallet_pubkey = this.block.cluster.wallet_pubkey;
//       if (wallet_pubkey == null) {
//           return null;
//       }
//       
//       return _level_up_tx({ entry_pubkey : this.pubkey,
//                             token_owner_pubkey : wallet_pubkey,
//                             token_pubkey : get_associated_token_pubkey(wallet_pubkey, this.mint_pubkey),
//                             entry_metaplex_metadata_pubkey : this.metaplex_metadata_pubkey,
//                             ki_source_pubkey : get_associated_token_pubkey(wallet_pubkey, g_ki_mint_pubkey),
//                             ki_source_owner_pubkey : wallet_pubkey });
//   }
//   
//   take_commission_or_delegate_tx()
//   {
//       let wallet_pubkey = this.block.cluster.wallet_pubkey;
//       if (wallet_pubkey == null) {
//           return null;
//       }
//       
//       return _take_commission_or_delegate_tx({ funding_pubkey : wallet_pubkey,
//                                                block_pubkey : this.block.pubkey,
//                                                entry_pubkey : this.pubkey,
//                                                stake_pubkey : this.owned_stake_account,
//                                                bridge_pubkey : get_entry_bridge_pubkey(this.mint_pubkey) });
//   }
}


function metaplex_metadata_pubkey(mint_pubkey)
{
    return SolanaWeb3.PublicKey.findProgramAddressSync([ Buffer.Buffer.from("metadata"),
                                                         g_metaplex_program_pubkey.toBuffer(),
                                                         mint_pubkey.toBuffer() ],
                                                       g_metaplex_program_pubkey)[0];
}


function get_block_pubkey(group_number, block_number)
{
    return SolanaWeb3.PublicKey.findProgramAddressSync([ [ 7 ],
                                                         u32_to_le_bytes(group_number),
                                                         u32_to_le_bytes(block_number) ],
                                                       g_nifty_program_pubkey)[0];
}


function get_entry_mint_pubkey(block_pubkey, entry_index)
{
    return SolanaWeb3.PublicKey.findProgramAddressSync([ [ 5 ],
                                                         block_pubkey.toBuffer(),
                                                         u16_to_le_bytes(entry_index) ],
                                                       g_nifty_program_pubkey)[0];
}


function get_entry_pubkey(entry_mint_pubkey)
{
    return SolanaWeb3.PublicKey.findProgramAddressSync([ [ 8 ],
                                                         entry_mint_pubkey.toBuffer() ],
                                                       g_nifty_program_pubkey)[0];
}


function get_entry_bridge_pubkey(entry_mint_pubkey)
{
    return SolanaWeb3.PublicKey.findProgramAddressSync([ [ 10 ],
                                                         entry_mint_pubkey.toBuffer() ],
                                                       g_nifty_program_pubkey)[0];
}


function get_associated_token_pubkey(token_owner_pubkey, token_mint_pubkey)
{
    return SolanaWeb3.PublicKey.findProgramAddressSync([ token_owner_pubkey.toBuffer(),
                                                         g_spl_token_program_pubkey.toBuffer(),
                                                         token_mint_pubkey.toBuffer() ],
                                                       g_spl_associated_token_program_pubkey)[0];
}


function get_bid_marker_token_pubkey(entry_mint_pubkey, bidder_pubkey)
{
    return SolanaWeb3.PublicKey.findProgramAddressSync([ [ 12 ],
                                                         entry_mint_pubkey.toBuffer(),
                                                         bidder_pubkey.toBuffer() ],
                                                       g_nifty_program_pubkey)[0];
}


function get_bid_pubkey(bid_marker_token_pubkey)
{
    return SolanaWeb3.PublicKey.findProgramAddressSync([ [ 9 ],
                                                         bid_marker_token_pubkey.toBuffer() ],
                                                       g_nifty_program_pubkey)[0];
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


function buffer_pubkey(buffer, offset)
{
    return new SolanaWeb3.PublicKey(buffer.slice(offset, offset + 32));
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
exports.nifty_program_pubkey = () => g_nifty_program_pubkey;
exports.config_pubkey = () => g_config_pubkey;
exports.master_stake_pubkey = () => g_master_stake_pubkey;
exports.ki_mint_pubkey = () => g_ki_mint_pubkey;
exports.ki_metadata_pubkey = () => g_ki_metadata_pubkey;
exports.bid_marker_mint_pubkey = () => g_bid_marker_mint_pubkey;
