'use strict';

// shinobi_immortals.js
//
// The higher level Shinobi Immortals Javascript API: querying the cluster, tracking blocks, entries, and wallets, and
// building, signing, and submitting transactions on behalf of a wallet.  It is built on shinobi_immortals_base.js,
// which holds the program's addresses, account decoders, calculations, and instruction builders.
//
// This file uses @solana/kit to talk to the cluster, and to build, sign, and send transactions.  The instructions of
// shinobi_immortals_base.js are already in @solana/kit's form.
//
// Transactions are signed by @solana/kit signers (see "Signing" in the Wallet class).  create_callback_signer makes a
// signer from a function that obtains a signature in any way at all, such as by showing the transaction to a person
// who signs it with their own tools.
//
// Nothing in this file needs Ed25519 support in the Javascript runtime's WebCrypto (crypto.subtle): signatures are
// verified by shinobi_immortals_base.js's own ed25519_verify.  Only @solana/kit's key pair signers (such as those of
// generateKeyPairSigner and createKeyPairSignerFromBytes), for callers that hold a secret key themselves, need it.

const Kit = require("@solana/kit");

const Base = require("./shinobi_immortals_base.js");

// Names from shinobi_immortals_base.js that are used in this file
const {
    base58_decode,
    g_system_program_address,
    g_config_program_address,
    g_validator_info_address,
    g_program_authority_address,
    g_program_state_address,
    g_shinobi_systems_vote_address,
    g_ki_mint_address,
    g_bid_marker_mint_address,
    g_spl_token_program_address,
    g_stake_program_address,
    WithdrawExcessAccountType,
    get_metaplex_metadata_address,
    get_block_address,
    get_entry_mint_address,
    get_entry_address,
    get_bid_address,
    decode_program_state,
    decode_authority,
    decode_block,
    decode_entry,
    compute_ki_harvest_retention,
    compute_retained_ki_harvest,
    compute_effective_stake_earnings,
    compute_price,
    compute_minimum_bid,
    excess_of,
    buffer_le_u32,
    buffer_le_u64,
    buffer_address,
    buffer_string
} = Base;

const BLOCKS_AT_ONCE = 3;
const ENTRIES_AT_ONCE = 20;


function delay(delay_ms)
{
    return new Promise((resolve) => setTimeout(resolve, delay_ms));
}


// Returns as a Uint8Array the bytes of account data as the RPC server returns it with base64 encoding: an array of
// the base64 string and the encoding name
function account_data_bytes(data)
{
    return Uint8Array.from(Kit.getBase64Encoder().encode(data[0]));
}


// Returns an account that the RPC server returned with base64 encoding, as { lamports : BigInt, data : Uint8Array,
// owner : string, executable : boolean }, or null if there was no account
function normalize_account(account)
{
    if (account == null) {
        return null;
    }

    return { lamports : BigInt(account.lamports),
             data : account_data_bytes(account.data),
             owner : account.owner,
             executable : account.executable };
}


// Single possibly rate-limited RPC connection, to one RPC server.  If a rate limit is set, it will observe it, and
// throw an error for any request that would exceed the limit.
//
// Its methods return values in plain forms, whatever the RPC server returns: addresses as base58 strings, account
// data as Uint8Arrays, and u64 values (lamports, slots, epochs) as BigInts, except where noted.
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

        this.rpc_endpoint = rpc_endpoint;

        // The @solana/kit RPC client for the server
        this.rpc = Kit.createSolanaRpc(rpc_endpoint);

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

    // Returns the cluster's genesis hash, as a string
    async get_genesis_hash()
    {
        return this.execute(() => this.rpc.getGenesisHash().send(), 1024);
    }

    // Returns the account at address, as normalize_account returns it, or null if there is none.  data_slice, if
    // given, is { offset, length }: only those bytes of the account's data are returned.
    async get_account(address, data_slice)
    {
        let config = { encoding : "base64", commitment : "confirmed" };
        if (data_slice != null) {
            config.dataSlice = data_slice;
        }
        let result = await this.execute(() => this.rpc.getAccountInfo(Kit.address(address), config).send(), 10 * 1024);
        return normalize_account(result.value);
    }

    // Returns the accounts at addresses, in the same order, each as get_account returns it
    async get_multiple_accounts(addresses)
    {
        let result = await this.execute(() => this.rpc.getMultipleAccounts(addresses.map((a) => Kit.address(a)),
                                                                           { encoding : "base64",
                                                                             commitment : "confirmed" }).send(),
                                        addresses.length * 10 * 1024);
        return result.value.map(normalize_account);
    }

    // Returns the account at address as the RPC server parses it ("jsonParsed" encoding): { lamports : BigInt, data },
    // where data is the server's parsed form ({ parsed, program, space }) for accounts that it can parse.  Returns
    // null if there is no account.
    async get_parsed_account(address)
    {
        let result = await this.execute(() => this.rpc.getAccountInfo(Kit.address(address),
                                                                      { encoding : "jsonParsed",
                                                                        commitment : "confirmed" }).send(),
                                        10 * 1024);
        return (result.value == null) ? null : { lamports : BigInt(result.value.lamports), data : result.value.data };
    }

    // Returns { epoch, absoluteSlot, blockHeight, slotIndex, slotsInEpoch }, each a BigInt
    async get_epoch_info()
    {
        return this.execute(() => this.rpc.getEpochInfo({ commitment : "confirmed" }).send(), 1024);
    }

    // Returns the estimated unix timestamp of the slot, as a BigInt, or null if it is not available
    async get_block_time(slot)
    {
        return this.execute(() => this.rpc.getBlockTime(BigInt(slot)).send(), 1024);
    }

    // Returns the current block height, as a BigInt
    async get_block_height()
    {
        return this.execute(() => this.rpc.getBlockHeight({ commitment : "confirmed" }).send(), 1024);
    }

    // Returns the lamports held by the account at address, as a BigInt
    async get_balance(address)
    {
        let result = await this.execute(() => this.rpc.getBalance(Kit.address(address),
                                                                  { commitment : "confirmed" }).send(),
                                        1024);
        return BigInt(result.value);
    }

    // Returns the token accounts owned by owner_address that belong to the token program at program_address, as
    // [ { address, account } ], where account is as get_parsed_account returns it
    async get_parsed_token_accounts_by_owner(owner_address, program_address)
    {
        let result = await this.execute(() => this.rpc.getTokenAccountsByOwner(
            Kit.address(owner_address), { programId : Kit.address(program_address) },
            { encoding : "jsonParsed", commitment : "confirmed" }).send(), 20 * 1024);
        return result.value.map((a) => ({ address : a.pubkey,
                                          account : { lamports : BigInt(a.account.lamports),
                                                      data : a.account.data } }));
    }

    // Returns the accounts of the program at program_address that pass filters (as the RPC server's
    // getProgramAccounts takes them), as [ { address, account } ], where account is as get_parsed_account returns it
    async get_parsed_program_accounts(program_address, filters)
    {
        let result = await this.execute(() => this.rpc.getProgramAccounts(
            Kit.address(program_address), { encoding : "jsonParsed", commitment : "confirmed", filters : filters })
                                        .send(), 20 * 1024);
        return result.map((a) => ({ address : a.pubkey,
                                    account : { lamports : BigInt(a.account.lamports), data : a.account.data } }));
    }

    // Returns the lamports that an account of data_length bytes must hold to be rent exempt, as a BigInt
    async get_minimum_balance_for_rent_exemption(data_length)
    {
        return this.execute(() => this.rpc.getMinimumBalanceForRentExemption(BigInt(data_length)).send(), 1024);
    }

    // Returns { blockhash, lastValidBlockHeight }: a recent blockhash, and the last block height (a BigInt) at which
    // a transaction using it can be processed, as @solana/kit's setTransactionMessageLifetimeUsingBlockhash takes them
    async get_latest_blockhash()
    {
        let result = await this.execute(() => this.rpc.getLatestBlockhash({ commitment : "confirmed" }).send(), 1024);
        return result.value;
    }

    // Sends the base64 encoded wire transaction to the cluster, returning its signature as a base58 string
    async send_transaction(wire_transaction_base64)
    {
        return this.execute(() => this.rpc.sendTransaction(wire_transaction_base64,
                                                           { encoding : "base64",
                                                             preflightCommitment : "confirmed" }).send(),
                            5 * 1024);
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
            throw new Error(this.rpc_endpoint + ": Too many outstanding requests");
        }

        // If there is a limit on the maximum amount of data, make sure that limit is not exceeded
        if ((this.max_data != null) && ((this.data_sum + data_size) > this.max_data.limit)) {
            throw new Error(this.rpc_endpoint + ": Too much request data");
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
                    }
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
                let genesis_hash = await rpc_connection.get_genesis_hash();

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
                (rpc_connections.array[0].rpc_endpoint == "http://localhost:8899")) {
                default_slot_duration_seconds = 0.1;
            }
            else {
                default_slot_duration_seconds = 0.62;
            }
        }

        if (slots_per_epoch == null) {
            if ((rpc_connections.array.length == 1) &&
                (rpc_connections.array[0].rpc_endpoint == "http://localhost:8899")) {
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
                return rpc_connection.get_parsed_account(stake_account_address);
            });
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
            stake.delegation = { lamports : BigInt(delegation.stake), vote_account : delegation.voter };

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
            return rpc_connection.get_account(g_program_authority_address);
        }, "get authority");

        let decoded = (account == null) ? null : decode_authority(account.data);

        if (decoded == null) {
            return null;
        }

        let rent_exempt_minimum_lamports = await this.get_rent_exempt_minimum(account.data.length);

        let authority = { address : g_program_authority_address,
                          lamports : account.lamports,
                          rent_exempt_minimum_lamports : rent_exempt_minimum_lamports,
                          mystery_escrow_lamports : decoded.mystery_escrow_lamports };

        // The account is required to hold its rent exempt minimum plus the escrow; anything beyond that is excess
        authority.excess_lamports = excess_of(authority.lamports,
                                              rent_exempt_minimum_lamports + authority.mystery_escrow_lamports);

        return authority;
    }

    // Returns as a BigInt the rent exempt minimum, in lamports, of an account with data_length bytes of data
    async get_rent_exempt_minimum(data_length)
    {
        return this.rpc_connections.run((rpc_connection) => {
            return rpc_connection.get_minimum_balance_for_rent_exemption(data_length);
        }, "get rent exempt minimum");
    }

    // Causes an entry to be immediately refreshed.  If this causes the Entry to change, asynchronously a callback
    // into the callbacks' on_entry_changed function will be made.
    async refresh_entry(entry)
    {
        let block_result = await this.rpc_connections.run((rpc_connection) => {
            return rpc_connection.get_account(entry.block.address);
        }, "refresh entry update block");

        let entry_result = await this.rpc_connections.run((rpc_connection) => {
            return rpc_connection.get_account(entry.address);
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
            return rpc_connection.get_epoch_info();
        }, "update_clock get epoch info");

        let block_time = await this.rpc_connections.run((rpc_connection) => {
            return rpc_connection.get_block_time(epoch_info.absoluteSlot);
        }, "update_clock get block time");

        // Update the current data.  The RPC server's values are BigInts; slots, epochs, and timestamps are kept as
        // Numbers, which they always fit, so that get_clock can do arithmetic with them.
        this.confirmed_query_time = Date.now();
        this.confirmed_epoch = Number(epoch_info.epoch);
        this.confirmed_slot = Number(epoch_info.absoluteSlot);
        this.confirmed_unix_timestamp = (block_time == null) ? null : Number(block_time);
    }

    async update_blocks(group_number, block_number)
    {
        let block_addresses = [ ];

        for (let i = 0; i < BLOCKS_AT_ONCE; i++) {
            block_addresses.push(get_block_address(group_number, block_number + i));
        }

        let results = await this.rpc_connections.run((rpc_connection) =>
            {
                return rpc_connection.get_multiple_accounts(block_addresses);
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
                return rpc_connection.get_multiple_accounts(entry_addresses);
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
                return rpc_connection.get_account(vote_account, { offset : 4, length : 32 });
            });
        }
        catch (error) {
            console.log(error);
            result = null;
        }

        if ((result == null) || (result.data == null)) {
            return null;
        }

        let validator_account = buffer_address(result.data, 0);

        try {
            result = await this.rpc_connections.run((rpc_connection) => {
                // Fetch the validator info account for the vote account
                return rpc_connection.get_parsed_program_accounts(g_config_program_address, [
                    // Config program entry that is storing validator info
                    { memcmp : { offset : 1n, bytes : g_validator_info_address, encoding : "base58" } },
                    // Validator info for the vote account
                    { memcmp : { offset : 34n, bytes : validator_account, encoding : "base58" } }
                ]);
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
        // Decode the block's fields (see decode_block in shinobi_immortals_base.js); block_start_timestamp is the time
        // at which the block was completed (0 until then), which is also the beginning of the life span of each of
        // its entries, over which their Ki harvests are reduced
        Object.assign(this, decode_block(data));
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
        // Decode the entry's fields (see decode_entry in shinobi_immortals_base.js)
        Object.assign(this, decode_entry(data));
    }

    update(data)
    {
        let new_entry = new Entry(this.block, this.address, data);

        // Only check for changes to fields that can change
        let changed = false;

        if (new_entry.reveal_sha256 != this.reveal_sha256) {
            this.reveal_sha256 = new_entry.reveal_sha256;
            // reveal_timestamp can only change when reveal_sha256 changes
            this.reveal_timestamp = new_entry.reveal_timestamp;
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
            return rpc_connection.get_account(this.metaplex_metadata_address);
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
            return compute_price(BigInt(this.block.mystery_phase_duration),
                                 this.block.mystery_start_price_lamports,
                                 this.block.minimum_price_lamports,
                                 Entry.seconds_since(clock, this.block.block_start_timestamp));
        }
        else if (this.has_auction) {
            return this.block.minimum_price_lamports;
        }
        else {
            return compute_price(BigInt(this.duration),
                                 this.non_auction_start_price_lamports,
                                 this.minimum_price_lamports,
                                 Entry.seconds_since(clock, this.reveal_timestamp));
        }
    }

    // This only returns a valid value if the entry state is InAuction
    get_auction_minimum_bid_lamports(clock)
    {
        return compute_minimum_bid(this.minimum_price_lamports, this.auction_highest_bid_lamports);
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
            return rpc_connection.get_account(this.owned_stake_account, { offset : 0, length : 180 });
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
            return rpc_connection.get_epoch_info();
        }, "fetch epoch info");

        if ((epoch_info == null) || (activation_epoch >= BigInt(epoch_info.epoch))) {
            return 0n;
        }

        // A decrease in delegated stake is not earnings (the program resets its baseline in that case)
        if (stake < this.owned_last_ki_harvest_stake_account_lamports) {
            return 0n;
        }

        // Apply the effective stake curve to what was earned
        let earnings = stake - this.owned_last_ki_harvest_stake_account_lamports;
        let effective_earnings = compute_effective_stake_earnings(earnings, stake);

        // Reduce it to the fraction that the entry retains at the current point of its life span, which begins when
        // its block was completed, and is measured in whole days.  The cluster's current time is the block time of the
        // slot that the epoch info was fetched at, just as update_clock obtains it.
        let block_time = await rpc_connections.run((rpc_connection) => {
            return rpc_connection.get_block_time(epoch_info.absoluteSlot);
        }, "fetch block time");
        if (block_time == null) {
            return 0n;
        }
        let lifespan_seconds = BigInt(block_time) - BigInt(this.block.block_start_timestamp);
        let lifespan_days = (lifespan_seconds > 0n) ? (lifespan_seconds / 86400n) : 0n;
        let retention = compute_ki_harvest_retention(this.metadata_ki_harvest_reduction_per_day, lifespan_days);

        // Then convert the retained earnings to whole Ki at the current level's ki_factor
        return compute_retained_ki_harvest(effective_earnings, this.level_metadata[this.level].ki_factor, retention);
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

        // While a transaction is being signed, how often to check whether its blockhash has expired
        this.expiry_check_interval_ms = 5000;

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

    // Returns the wallet's balance, in lamports, as a BigInt
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
                return rpc_connection.get_balance(wallet_address);
            }, "update sol balance");

            if (wallet_address_holder == this.wallet_address_holder) {
                this.sol_balance = sol_balance;
                this.last_balance_fetch_time = Date.now();
            }
        }

        return this.sol_balance;
    }

    // Returns the wallet's Ki, in whole Ki, as a BigInt
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

    // Returns an iterator over { entry_address, bid_address, lamports }.  lamports (a BigInt) is the number of
    // lamports in the bid account, which could be more than the bid if lamports were added post-bid.
    async get_bids()
    {
        await this.update_token_data();

        return this.bids_by_entry_address.values();
    }

    // Returns an iterator over { address : string,
    //                            lamports : BigInt (only if not delegated),
    //                            delegation : null or { lamports : BigInt,
    //                                                   vote_account : string,
    //                                                   validator_name : string or null },
    //                            is_immortal_staked : true or null
    //                          }
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
                    return rpc_connection.get_parsed_token_accounts_by_owner(wallet_address,
                                                                             g_spl_token_program_address);
                }, "update token data token accounts");

            let new_ki_balance = 0n;

            let new_entry_addresses = new Set();

            let new_bids_by_entry_address = new Map();

            let new_bids_by_entry_mint = new Map();

            let promises = [ ];

            for (let idx = 0; idx < results.length; idx++) {
                let account = results[idx].account;
                let address = results[idx].address;

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
                    new_ki_balance += (BigInt(account.data.parsed.info.tokenAmount.amount) / 10n);
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

        let now = Date.now();

        // If never fetched stakes, fetch them
        if ((this.last_stakes_fetch_time == null) ||
            // If haven't fetched stakes in 30 seconds, fetch them
            ((now - this.last_stakes_fetch_time) > 30000)) {
            let results = await this.cluster.rpc_connections.run((rpc_connection) =>
                {
                    // The withdraw authority of a stake account is at offset 44 of its data
                    return rpc_connection.get_parsed_program_accounts(g_stake_program_address, [
                        { memcmp : { offset : 44n, bytes : wallet_address, encoding : "base58" } }
                    ]);
                }, "update stakes stake accounts");

            if (wallet_address_holder != this.wallet_address_holder) {
                return;
            }

            // Build the new set of stake accounts separately, and only replace this.stakes_by_address once it is
            // complete, so that callers never see a partially built set
            let new_stakes_by_address = new Map();

            for (let idx = 0; idx < results.length; idx++) {
                let account = results[idx].account;
                let stake_address = results[idx].address;

                if ((account == null) || (account.data == null) || (account.data.parsed == null) ||
                    (account.data.parsed.info == null)) {
                    continue;
                }

                // Ignore stake accounts that are not initialized or delegated
                if ((account.data.parsed.type != "initialized") &&
                    (account.data.parsed.type != "delegated")) {
                    continue;
                }

                // Ignore stake accounts with lockup (which the RPC server gives in the stake account's meta)
                let lockup = account.data.parsed.info.meta.lockup;
                if (lockup && ((lockup.epoch != 0) || (lockup.unixTimestamp != 0))) {
                    continue;
                }

                let withdrawer = account.data.parsed.info.meta.authorized.withdrawer;

                if (withdrawer != wallet_address) {
                    continue;
                }

                let stake = await this.cluster.get_stake_account(stake_address, account);

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
                return rpc_connection.get_account(bid_address, { offset : 4, length : 32 });
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
            return rpc_connection.get_account(get_metaplex_metadata_address(mint_address));
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

    // Returns the program state, as decoded by decode_program_state (in shinobi_immortals_base.js), or null if it
    // cannot be fetched.  Its reward_rate is the reward rate that LevelUp uses (see level_up_reward_bps in
    // shinobi_immortals_base.js).
    async fetch_program_state()
    {
        let result = await this.cluster.rpc_connections.run((rpc_connection) =>
            {
                return rpc_connection.get_account(g_program_state_address);
            }, "fetch program state");

        return (result == null) ? null : decode_program_state(result.data);
    }

    // Returns the admin's address as a string, or null if it cannot be determined
    async fetch_admin_address()
    {
        let program_state = await this.fetch_program_state();

        return (program_state == null) ? null : program_state.admin_address;
    }

    // Signing --------------------------------------------------------------------------------------------------------
    //
    // Each of the following builds a transaction holding one instruction of the program, has it signed by signer,
    // and sends it, returning its signature (the transaction id, as a base58 string).  Each throws an error on any
    // failure, including the signer's refusal to sign.
    //
    // signer is a @solana/kit transaction signer whose address is the wallet's address: a TransactionPartialSigner
    // (such as a KeyPairSigner, or a signer made by create_callback_signer), or a TransactionModifyingSigner.  The
    // wallet pays the transaction's fee.
    //
    // The signed transaction is only sent if it is exactly the transaction that was built (a modifying signer may
    // not change it), and if every signature in it is valid.  So what is sent is always exactly what the signer was
    // shown, and was signed by the signer's key.
    //
    // A transaction can only be processed until its blockhash expires, about a minute after it is built.  If the
    // blockhash expires before the transaction is signed, signing is aborted (the signer is given an AbortSignal,
    // which is then aborted), and the transaction is built again with a new blockhash and given to the signer again.
    // A signer that takes a long time to sign (such as one that waits for a person) should stop waiting when its
    // AbortSignal is aborted.

    async buy_entry(entry, maximum_price_lamports, signer)
    {
        let admin_address = await this.fetch_admin_address_or_throw();

        return this.complete_tx((wallet_address) => {
            return Base.make_buy_instruction(entry, maximum_price_lamports, wallet_address, admin_address);
        }, signer);
    }

    async refund_entry(entry, signer)
    {
        return this.complete_tx((wallet_address) => {
            return Base.make_refund_instruction(entry, wallet_address);
        }, signer);
    }

    async bid_on_entry(entry, minimum_bid_lamports, maximum_bid_lamports, signer)
    {
        return this.complete_tx((wallet_address) => {
            return Base.make_bid_instruction(entry, minimum_bid_lamports, maximum_bid_lamports, wallet_address);
        }, signer);
    }

    // Claims the entry if won is true (the wallet won its auction), or reclaims the wallet's losing bid on it if not
    async claim_entry(entry, won, signer)
    {
        if (won) {
            let admin_address = await this.fetch_admin_address_or_throw();

            // Claiming a winning bid is settling it, with the wallet as both the funding account and the bidder
            return this.complete_tx((wallet_address) => {
                return Base.make_settle_winning_bid_instruction(entry, wallet_address, admin_address, wallet_address);
            }, signer);
        }

        return this.complete_tx((wallet_address) => {
            return Base.make_claim_losing_instruction(entry, wallet_address);
        }, signer);
    }

    // Stakes the entry, which the wallet must own, to stake_account, whose withdraw authority must be the wallet.  The
    // stake account is delegated to Shinobi Systems (the only vote account that Stake accepts), and the token account
    // holding the entry's token is frozen until the entry is destaked.
    async stake_entry(entry, stake_account, signer)
    {
        return this.complete_tx((wallet_address) => {
            return Base.make_stake_instruction(entry, stake_account, wallet_address);
        }, signer);
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
    async destake_entry(entry, signer, no_ki_harvest, no_thaw)
    {
        return this.complete_tx((wallet_address) => {
            return Base.make_destake_instruction(entry, wallet_address, no_ki_harvest, no_thaw);
        }, signer);
    }

    // Thaws the entry's token account, held by the wallet, when it was left frozen by a destake with no_thaw.  This
    // only succeeds if the entry is not staked and its token account is frozen.
    async thaw_entry(entry, signer)
    {
        return this.complete_tx((wallet_address) => {
            return Base.make_thaw_instruction(entry, wallet_address);
        }, signer);
    }

    async harvest_entry(entry, signer)
    {
        return this.complete_tx((wallet_address) => {
            return Base.make_harvest_instruction(entry, wallet_address);
        }, signer);
    }

    // ki_to_apply is whole Ki; use a decimal string for large integer amounts.
    async level_up_entry(entry, ki_to_apply, signer)
    {
        return this.complete_tx((wallet_address) => {
            return Base.make_level_up_instruction(entry, ki_to_apply, wallet_address);
        }, signer);
    }

    // Settles the winning bid of an auction that has ended, on behalf of its bidder: the entry's token goes to the
    // associated token account of bidder_address, the winning bid goes to the admin, and the entry becomes owned.
    // Any wallet may do this, but only once 30 days have passed since the end of the auction; until then only the
    // winner may settle, which the winner does with claim_entry(entry, true, signer).  The wallet pays the
    // transaction fee, and for the creation of the bidder's associated token account if it does not exist yet.
    // bidder_address must be the address of the bidder who won the auction.
    async settle_winning_bid(entry, bidder_address, signer)
    {
        let admin_address = await this.fetch_admin_address_or_throw();

        return this.complete_tx((wallet_address) => {
            return Base.make_settle_winning_bid_instruction(entry, bidder_address, admin_address, wallet_address);
        }, signer);
    }

    // Delegates the stake account of a staked entry to Shinobi Systems.  This succeeds only if the stake account is
    // not actively delegated, which is the case when it was staked to the entry while undelegated or delegated to
    // another validator (Stake deactivates it in the latter case; the delegation then succeeds once it has cooled
    // down).  Any wallet may do this; the wallet only pays the transaction fee.
    async delegate(entry, signer)
    {
        return this.complete_tx((wallet_address) => {
            return Base.make_delegate_instruction(entry);
        }, signer);
    }

    // Brings the entry's epochs_staked up to date, so that it is exact as of the current epoch.  If the entry is not
    // staked, this does nothing, since its epochs_staked is already exact.  Any wallet may do this; the wallet only
    // pays the transaction fee.  A game that needs an exact epochs_staked would normally not use this, but would
    // instead include the instruction returned by make_update_epochs_staked_instruction (in
    // shinobi_immortals_base.js) in its own transaction, before its own instruction.
    async update_epochs_staked(entry, signer)
    {
        return this.complete_tx((wallet_address) => {
            return Base.make_update_epochs_staked_instruction(entry);
        }, signer);
    }

    // Measures the reward rate that a delegator to Shinobi Systems earns, and records it for LevelUp to use; at most
    // once per level credit window (see make_measure_reward_rate_instruction in shinobi_immortals_base.js).  If
    // require_measured is true, the transaction fails if the rate has not been measured in the current window and
    // cannot be measured now.  Any wallet may do this; the wallet only pays the transaction fee.  A wallet that levels
    // up would normally include the instruction returned by make_measure_reward_rate_instruction in the same
    // transaction as LevelUp, before it, rather than use this.
    async measure_reward_rate(require_measured, signer)
    {
        return this.complete_tx((wallet_address) => {
            return Base.make_measure_reward_rate_instruction(require_measured);
        }, signer);
    }

    // Withdraws to the wallet the excess lamports of an account that the program controls: everything that the
    // account holds beyond what it is required to hold.  The program computes the amount, and fails the transaction
    // if there is no excess.  This only succeeds if the wallet is the admin.
    //
    // account_type is one of the WithdrawExcessAccountType values.  account identifies the account to withdraw from:
    // - for ProgramState and Authority, it is ignored
    // - for Block and Whitelist, it is the Block (or the string address of the block account)
    // - for Entry, it is the Entry (or the string address of the entry account)
    async withdraw_excess(account_type, account, signer)
    {
        return this.complete_tx((wallet_address) => {
            return Base.make_withdraw_excess_instruction(account_type, account, wallet_address);
        }, signer);
    }

    // Private implementation follows ---------------------------------------------------------------------------------

    // Returns the admin's address as a string, throwing an error if it cannot be determined
    async fetch_admin_address_or_throw()
    {
        let admin_address = await this.fetch_admin_address();

        if (admin_address == null) {
            throw new Error("Admin address could not be determined");
        }

        return admin_address;
    }

    // Builds a transaction holding the single instruction returned by instruction_maker_func(wallet_address), has
    // signer sign it, and sends it, returning its signature.  See "Signing" above.
    async complete_tx(instruction_maker_func, signer)
    {
        let wallet_address_holder = this.wallet_address_holder;
        let wallet_address = wallet_address_holder.wallet_address;
        if (wallet_address == null) {
            throw new Error("No wallet");
        }

        check_signer(signer, wallet_address);

        let instruction = await instruction_maker_func(wallet_address);

        // Build, sign, and send the transaction; repeated with a new blockhash whenever the blockhash expires before
        // the transaction is signed
        while (true) {
            let lifetime = await this.cluster.rpc_connections.run((rpc_connection) => {
                return rpc_connection.get_latest_blockhash();
            }, "fetch latest blockhash");

            if (wallet_address_holder != this.wallet_address_holder) {
                throw new Error("Wallet change");
            }

            // A legacy transaction, which every tool can decode, with the wallet's signer as fee payer
            let message = Kit.pipe(
                Kit.createTransactionMessage({ version : "legacy" }),
                (m) => Kit.setTransactionMessageFeePayerSigner(signer, m),
                (m) => Kit.setTransactionMessageLifetimeUsingBlockhash(lifetime, m),
                (m) => Kit.appendTransactionMessageInstruction(instruction, m));

            // The bytes that are signed; the signed transaction must hold exactly these
            let message_bytes = Kit.compileTransaction(message).messageBytes;

            // Have the transaction signed, aborting the signing if the blockhash expires first
            let expiry = this.watch_for_expiry(lifetime.lastValidBlockHeight);
            let signed;
            try {
                signed = await Kit.signTransactionMessageWithSigners(message, { abortSignal : expiry.signal });
            }
            catch (error) {
                if (expiry.expired) {
                    continue;
                }
                throw error;
            }
            finally {
                expiry.stop();
            }

            // A signer that does not stop when aborted may still sign after the blockhash has expired; such a
            // transaction would be rejected, so build it again
            if (expiry.expired) {
                continue;
            }

            if (wallet_address_holder != this.wallet_address_holder) {
                throw new Error("Wallet change");
            }

            if (!bytes_equal(signed.messageBytes, message_bytes)) {
                throw new Error("The signer changed the transaction; it was not sent");
            }

            verify_transaction_signatures(signed);

            return this.send_transaction(Kit.getBase64EncodedWireTransaction(signed));
        }
    }

    // Returns { signal, expired, stop }: signal is an AbortSignal that is aborted, and expired becomes true, once the
    // cluster's block height passes last_valid_block_height (when a transaction using the blockhash that it belongs
    // to can no longer be processed).  The block height is checked every expiry_check_interval_ms milliseconds;
    // stop() stops checking.
    watch_for_expiry(last_valid_block_height)
    {
        let controller = new AbortController();
        let watcher = { signal : controller.signal, expired : false };

        let timer = setInterval(async () => {
            try {
                let block_height = await this.cluster.rpc_connections.run_once((rpc_connection) => {
                    return rpc_connection.get_block_height();
                }, "fetch block height");
                if ((block_height > BigInt(last_valid_block_height)) && !watcher.expired) {
                    watcher.expired = true;
                    controller.abort(new Error("The transaction's blockhash expired before it was signed"));
                }
            }
            catch (error) {
                // Check again at the next interval
            }
        }, this.expiry_check_interval_ms);

        watcher.stop = () => clearInterval(timer);

        return watcher;
    }

    // Sends a signed transaction (as a base64 encoded wire transaction), returning its signature
    async send_transaction(wire_transaction_base64)
    {
        // Retry up to 4 times with a 1 second delay in between each retry
        let retry_count = 0;
        while (true) {
            try {
                return await this.cluster.rpc_connections.run_once((rpc_connection) => {
                    return rpc_connection.send_transaction(wire_transaction_base64);
                }, "send transaction");
            }
            catch (error) {
                // A transaction that failed when the RPC server simulated it would fail again; report why, with its
                // programs' logs, at once
                const PREFLIGHT_FAILURE = Kit.SOLANA_ERROR__JSON_RPC__SERVER_ERROR_SEND_TRANSACTION_PREFLIGHT_FAILURE;
                if (Kit.isSolanaError(error, PREFLIGHT_FAILURE)) {
                    let reason = ((error.cause != null) && error.cause.message) ? error.cause.message : error.message;
                    let logs = ((error.context != null) && Array.isArray(error.context.logs)) ?
                        ("\n" + error.context.logs.join("\n")) : "";
                    throw new Error("The transaction failed: " + reason + logs, { cause : error });
                }
                // Otherwise the transaction may not have reached the server; try again
                if (retry_count++ == 4) {
                    throw new Error("Failed to send transaction: " + error.message, { cause : error });
                }
                await delay(1000);
            }
        }
    }
}


// Signers ------------------------------------------------------------------------------------------------------------


// Throws an error unless signer is a @solana/kit signer that can be used to sign a transaction for the wallet at
// wallet_address: a TransactionPartialSigner or TransactionModifyingSigner, whose address is wallet_address
function check_signer(signer, wallet_address)
{
    if ((signer == null) || (typeof(signer) != "object")) {
        throw new Error("A signer is required");
    }

    if (signer.address != wallet_address) {
        throw new Error("The signer's address " + signer.address + " is not the wallet's address " + wallet_address);
    }

    if (!Kit.isTransactionPartialSigner(signer) && !Kit.isTransactionModifyingSigner(signer)) {
        throw new Error("The signer must be a @solana/kit TransactionPartialSigner or TransactionModifyingSigner");
    }
}


// Returns true if the two arrays of bytes are equal
function bytes_equal(a, b)
{
    if (a.length != b.length) {
        return false;
    }
    for (let i = 0; i < a.length; i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}


// Returns true if signature (64 bytes) is a valid signature by the key of address of message_bytes, as the Solana
// runtime would judge it.  This uses the ed25519 verification of shinobi_immortals_base.js rather than @solana/kit's
// verifySignature, because the latter needs Ed25519 support in the Javascript runtime's WebCrypto (crypto.subtle),
// which not every runtime has; with this, nothing in this file needs WebCrypto.
function verify_signature(address, signature, message_bytes)
{
    return Base.ed25519_verify(Base.address_to_bytes(address), signature, message_bytes);
}


// Throws an error unless every signature of a @solana/kit Transaction is present and valid
function verify_transaction_signatures(transaction)
{
    for (let [ address, signature ] of Object.entries(transaction.signatures)) {
        if (signature == null) {
            throw new Error("The transaction is not signed by " + address);
        }
        if (!verify_signature(address, signature, transaction.messageBytes)) {
            throw new Error("The signature of " + address + " is not valid for the transaction");
        }
    }
}


// Returns a @solana/kit TransactionPartialSigner for address, which obtains each signature from callback, which may
// obtain it in any way: for example, by showing the transaction to a person, who checks it and signs it with their
// own tools, and then gives back its signature.  callback is called once for each transaction to be signed as:
//
//     async callback(transaction_base64, abort_signal)
//
// transaction_base64 is the transaction, as a base64 encoded wire transaction, with an empty (all zero) signature
// for each of its signers.  Any tool that decodes Solana transactions can show it; for example, with solxact
// (https://github.com/bji/solxact), the following shows it, and then signs it and prints the signature:
//
//     echo <transaction_base64> | base64 -d | solxact decode
//     echo <transaction_base64> | base64 -d | solxact sign <key file> | solxact signature
//
// abort_signal is an AbortSignal (or undefined) which is aborted if the signature is no longer wanted, for example
// because the transaction expired before it was signed.  The callback should then stop waiting, and throw (such as
// abort_signal.reason).
//
// callback returns the signature by address of the transaction: a base58 string (as "solxact signature" prints it),
// or 64 bytes.  To decline to sign, it throws.  The signature is checked, and an invalid one throws an error.
function create_callback_signer(address, callback)
{
    let signer_address = Kit.address(address);

    return Object.freeze({
        address : signer_address,

        async signTransactions(transactions, config)
        {
            let abort_signal = (config == null) ? undefined : config.abortSignal;

            let signature_dictionaries = [ ];

            for (let transaction of transactions) {
                if (abort_signal != null) {
                    abort_signal.throwIfAborted();
                }

                let returned = await callback(Kit.getBase64EncodedWireTransaction(transaction), abort_signal);

                let signature = (typeof(returned) == "string") ? base58_decode(returned.trim()) :
                    Uint8Array.from(returned);

                if (signature.length != 64) {
                    throw new Error("A signature must be 64 bytes, not " + signature.length);
                }

                if (!verify_signature(signer_address, signature, transaction.messageBytes)) {
                    throw new Error("The signature is not " + signer_address + "'s signature of the transaction");
                }

                signature_dictionaries.push(Object.freeze({ [signer_address] : signature }));
            }

            return signature_dictionaries;
        }
    });
}


exports.RpcConnections = RpcConnections;
exports.Cluster = Cluster;
exports.Block = Block;
exports.EntryState = EntryState;
exports.Entry = Entry;
exports.Wallet = Wallet;
exports.WithdrawExcessAccountType = WithdrawExcessAccountType;
exports.RpcConnection = RpcConnection;
exports.create_callback_signer = create_callback_signer;
// Everything in shinobi_immortals_base.js is also available here
exports.Base = Base;
