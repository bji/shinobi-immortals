'use strict';

const SolanaWeb3 = require('@solana/web3.js');
const Buffer = require('buffer');
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

const BLOCKS_AT_ONCE = 5;
const ENTRIES_AT_ONCE = 25;

const g_nifty_program_pubkey = new SolanaWeb3.PublicKey(NIFTY_PROGRAM_ADDRESS);
const g_metaplex_program_pubkey = new SolanaWeb3.PublicKey(METAPLEX_PROGRAM_ADDRESS);
const g_spl_token_program_pubkey = new SolanaWeb3.PublicKey(SPL_TOKEN_PROGRAM_ADDRESS);
const g_spl_associated_token_program_pubkey = new SolanaWeb3.PublicKey(SPL_ASSOCIATED_TOKEN_PROGRAM_ADDRESS);
const g_stake_program_pubkey = new SolanaWeb3.PublicKey(STAKE_PROGRAM_ADDRESS);

const g_config_pubkey = SolanaWeb3.PublicKey.findProgramAddressSync([ [ 1 ] ], g_nifty_program_pubkey)[0];

const g_master_stake_pubkey = SolanaWeb3.PublicKey.findProgramAddressSync([ [ 3 ] ], g_nifty_program_pubkey)[0];

const g_ki_mint_pubkey = SolanaWeb3.PublicKey.findProgramAddressSync([ [ 4 ] ], g_nifty_program_pubkey)[0];

const g_ki_metadata_pubkey = metaplex_metadata_pubkey(g_ki_mint_pubkey);

const g_bid_marker_mint_pubkey = SolanaWeb3.PublicKey.findProgramAddressSync([ [ 11 ] ], g_nifty_program_pubkey)[0];


function delayed_promise(resolve, delay_ms)
{
    return new Promise(() => setTimeout(resolve, delay_ms));
}


// RpcConnections holds a set of endpoints connecting to a single cluster.  It provides functions for executing
// functions against a cluster in a round-robin fashion; and running a function repeatedly on the cluster, until
// stopped.
class RpcConnections
{
    static create()
    {
        return new RpcConnections();
    }

    // Do not use until the successful completion of the first update() call.
    constructor()
    {
        // Map from rpc_endpoint to rpc_connection
        this.map = new Map();

        // List of rpc_connections (not set here since initialized in update)
        // this.array = [ ];

        // Next rpc_connection to use (not set here since initialized in update)
        // this.index = 0;
    }

    // If rpc_endpoints is null, a default is used.  Throws an error if it failed because any one of new_rpc_endpoints
    // was not for the same cluster as this RpcConnections instance.
    async update(new_rpc_endpoints)
    {
        // If new_rpc_endpoints is null, then use default set of rpc endpoints
        if (new_rpc_endpoints == null) {
            new_rpc_endpoints = [ "https://api.mainnet-beta.solana.com", "https://ssc-dao.genesysgo.net" ];
        }
        else if (!Array.isArray(new_rpc_endpoints)) {
            new_rpc_endpoints = [ new_rpc_endpoints ];
        }

        // Create a new map and array from new_rpc_endpoints (re-using existing RPC connections)
        let new_map = new Map();
        let new_array = [ ];
        
        for (let rpc_endpoint of new_rpc_endpoints) {
            let rpc_connection = this.map.get(rpc_endpoint);

            if (rpc_connection == null) {
                rpc_connection = new SolanaWeb3.Connection(rpc_endpoint, "confirmed");

                // Check to make sure that the genesis hash is consistent
                let genesis_hash = await rpc_connection.getGenesisHash();
                
                if (this.genesis_hash == null) {
                    this.genesis_hash = genesis_hash;
                }
                else if (genesis_hash != this.genesis_hash) {
                    throw new Error("invalid rpc_endpoint");
                }
            }

            new_array.push(rpc_connection);
            
            new_map.set(rpc_endpoint, rpc_connection);
        }

        this.map = new_map;

        this.array = new_array;
        
        this.index = 0;
    }
    
    // Runs the async function, passing it an rpc_connection to use, and returning the result.  If the function throws
    // an error, retries it after 1 second.  If the RpcConnections is shut down, throws an error.
    async run(func, error_prefix)
    {
        try {
            let result = await func(this.get());

            if (this.is_shutdown) {
                throw new Error("shutdown");
            }

            return result;
        }
        catch (error) {
            if (this.is_shutdown) {
                throw new Error("shutdown");
            }
            
            console.log("error " + error_prefix + ": " + error);

            return delayed_promise(() => { return this.run(func, error_prefix); }, 1 * 1000);
        }
    }

    // Runs an async function at fixed intervals until the RpcConnections is shutdown.
    // - When the function throws an error, re-runs it after 1 second
    // - When the function completes successfully, re-runs it after 1 minute
    async loop(func, interval_milliseconds)
    {
        // Exit the loop when the RpcConnections is shut down
        if (this.is_shutdown) {
            return;
        }
        
        try {
            await func();

            // On success, re-run the loop after [interval_milliseconds]
            return delayed_promise(() => { return this.loop(func, interval_milliseconds); }, interval_milliseconds);
        }
        catch (error) {
            // On error, re-run the loop after 1 second
            return delayed_promise(() => { return this.loop(func, interval_milliseconds); }, 1 * 1000);
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

    has(rpc_connection)
    {
        return this.map.has(rpc_connection.rpcEndpoint);
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
        return rpc_connections.run((rpc_connection) => {
            return rpc_connection.getAccountInfo(this.metaplex_metadata_pubkey);
        }, "fetch metaplex metadata").then((result) =>
            {
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
            });
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

    // Returns the pubkey of a bid marker for an auction bid for this Entry.  Only returns a value if
    // The cluster has a wallet_pubkey set, as the pubkey returned will be associated with that wallet.
//    get_bid_marker_token_pubkey()
//    {
//        let wallet_pubkey = this.block.cluster.wallet_pubkey;
//        if (wallet_pubkey == null) {
//            return null;
//        }
//
//        return get_bid_marker_token_pubkey(this.mint_pubkey, wallet_pubkey);
//    }
//
//    get_bid_pubkey()
//    {
//        let bid_marker_token_pubkey = this.get_bid_marker_token_pubkey();
//        if (bid_marker_token_pubkey == null) {
//            return null;
//        }
//
//        return get_bid_pubkey(bid_marker_token_pubkey);
//    }

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


// Enumeration of wallet item types
const WalletItemType = Object.freeze(
{
    // Lamports owned by wallet.  wallet_item is:
    // {
    //     wallet_item_type : WalletItemType.Lamports,
    //     lamports : int
    // }
    Lamports        : Symbol("Lamports"),
    // Ki owned by wallet.  wallet_item is:
    // {
    //     wallet_item_type : WalletItemType.Ki,
    //     ki_account_pubkey : pubkey,
    //     ki : int
    // }
    Ki              : Symbol("Ki"),
    // Entry owned by the wallet.  wallet_item is:
    // {
    //     wallet_item_type : WalletItemType.Entry,
    //     entry : Entry
    // }
    Entry           : Symbol("Entry"),
    // Bid by the wallet.  wallet_item is:
    // {
    //     wallet_item_type : WalletItemType.Bid,
    //     bid_marker_pubkey : pubkey,
    //     bid_account_pubkey : pubkey,
    //     entry : Entry
    // }
    Bid             : Symbol("Bid"),
    // Entry is in a stake account whose withdraw authority is the user.  wallet_item is:
    // {
    //     wallet_item_type : WalletItemType.Stake,
    //     stake_account_pubkey : pubkey,
    //     balance_lamports : int,
    //     delegated_lamports : int, // Only set if stake is delegated
    //     delegated_vote_account_pubkey : pubkey // Only set if stake is delegated
    // }
    Stake           : Symbol("Stake")
});


// View of a Cluster that supplies information about the contents of the wallet, and provides wallet-specific actions
// All pubkeys passed into and returned from Wallet are strings.
class Wallet
{
    // callbacks.submit_tx must be a function which accepts a Transaction that has no recent_blockhash set, and sets
    // the the recent_blockhash on the Transaction and submits it, returning a Promise
    static create(rpc_connections, wallet_pubkey, callbacks)
    {
        return new Wallet(rpc_connections, wallet_pubkey, callbacks);
    }

    // wallet_pubkey is a base-58 string
    constructor(rpc_connections, wallet_pubkey, callbacks)
    {
        this.rpc_connections = rpc_connections;

        this.wallet_pubkey_holder = { wallet_pubkey : null };
        
        this.callbacks = callbacks;

        this.set_wallet_pubkey(wallet_pubkey);

        // Fetch SOL balance only twice every 5 seconds
        // this.last_balance_fetch_time = 0;
        // this.last_balance_fetch_count = 0;
        // this.sol_balance = null;

        // Fetch wallet tokens only twice every 60 seconds
        // this.last_tokens_fetch_time = 0;
        // this.last_tokens_fetch_count = 0;
        // xxx tokens

        // Fetch wallet stakes only twice every 60 seconds
        // this.last_stakes_fetch_time = 0;
        // this.last_stakes_fetch_count = 0;
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
        this.last_balance_fetch_count = 0;
        this.sol_balance = null;
        this.last_tokens_fetch_time = null;
        this.last_tokens_fetch_count = 0;
        this.last_stakes_fetch_time = null;
        this.last_stakes_fetch_count = 0;
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
            ((now - this.last_balance_fetch_time) > 15000) ||
            // If fetched balance twice already in the previous 5 seconds, fetch it
            (this.last_balance_fetch_count == 3)) {

            // Wait on getting SOL balance of wallet
            let sol_balance = await this.rpc_connections.run((rpc_connection) => {
                return rpc_connection.getBalance(wallet_pubkey);
            }, "update sol balance");
            
            if (wallet_pubkey_holder == this.wallet_pubkey_holder) {
                this.sol_balance = sol_balance;
                this.last_balance_fetch_time = Date.now();
                this.last_balance_fetch_count = 1;
            }
        }
        else {
            this.last_balance_fetch_count += 1;
        }
            
        return this.sol_balance;
    }

    async update_tokens()
    {
    }

    async update_stakes()
    {
    }
    
//    async query_wallet()
//    {
//        try {
//            let rpc_connection = this.rpc_connections.get();
//            // Save wallet_pubkey_holder so that we know if it changed
//            let wallet_pubkey_holder = this.wallet_pubkey_holder;
//            let wallet_pubkey = this.wallet_pubkey_holder.pubkey;
//            
//            // Create promises to wait on
//            let promises = [ ];
//            
//            let sol_balance;
//            
//            // Wait on getting SOL balance of wallet
//            let result = await rpc_connection.getBalance(entry.block.pubkey).then((b) => sol_balance = b);
//            
//            await Promises.all(promises);
//            
//            // If the rpc or wallet config has changed, ignore results
//            if (!this.rpc_connections.has(rpc_connection) || (this.wallet_pubkey_holder != wallet_pubkey_holder)) {
//                return;
//            }
//            
//            // If the balance has changed, callback to indicate this
//            if ((sol_balance != null) && (sol_balance != this.sol_balance)) {
//                this.sol_balance = sol_balance;
//                if ((callbacks != null) && (callbacks.on_sol_balance_changed != null)) {
//                    callbacks.on_sol_balance_changed(sol_balance);
//                    // If the state changed such that this query is out of date, stop querying the wallet
//                    if (!rpc_connections.has(rpc_connection) || (this.wallet_pubkey_holder != wallet_pubkey_holder)) {
//                        // Restart the query
//                        
//                    }
//                }
//            }
//        }
//        
//        // Wait on getting results of all token accounts owned by the wallet
//        promises.push(this.rpc_connection.getParsedTokenAccountsByOwner
//                      (this.wallet_pubkey, { programId : g_spl_token_program_pubkey }).then(() results = r));
//        
//        await Promises.all(promises);
//        
//        // After async call, check to make sure that rpc connection is still valid, and that the wallet pubkey
//        // holder has not changed to a new one
//        if (!this.rpc_connections.has(rpc_connection) || (wallet_pubkey_holder != this.wallet_pubkey_holder)) {
//            // Try again immediately
//            setTimeout(() => this.query_wallet(), 0);
//            return;
//        }
//        
//        // Promises to wait on to complete remaining operationsn
//        promises = [ ];
//        
//        // Wallet-owned entries
//        let entries = [ ];
//        
//        // Wallet-owned bids
//        let bids = [ ];
//        
//        for (let idx = 0; idx < results.value.length; idx++) {
//            let account = results.value[idx].account;
//            let pubkey = results.value[idx].pubkey;
//            
//            if ((account == null) || (account.data == null) || (account.data.parsed == null) ||
//                (account.data.parsed.info == null)) {
//                continue;
//            }
//            
//            if (account.data.parsed.info.state != "initialized") {
//                continue;
//            }
//            
//            if (account.data.parsed.info.tokenAmount.amount == 0) {
//                continue;
//            }
//            
//            // Should never happen but just in case
//            if (account.data.parsed.info.owner != wallet_pubkey.toBase58()) {
//                continue;
//            }
//            
//            let mint_pubkey = new SolanaWeb3.PublicKey(account.data.parsed.info.mint);
//            
//            let amount = 
//                
//            let token = {
//                mint_pubkey : ),
//                
//                account_pubkey : pubkey,
//                
//                amount : account.data.parsed.info.tokenAmount.amount
//        };
//        
//        tokens.push(token);
//        
//        if (token.mint_pubkey.equals(g_bid_marker_mint_pubkey)) {
//            promises.push(this.lookup_bid_entry_mint_pubkey(pubkey)
//                          .then((bid_entry_mint_pubkey) =>
//                              {
//                                  if (bid_entry_mint_pubkey == null) {
//                                      return;
//                                  }
//                                  
//                                  return this.get_entry(get_entry_pubkey(bid_entry_mint_pubkey));
//                              })
//                          .then((entry) =>
//                                     {
//                                         entries.push(entry);
//                                     }));
//               }
//               else {
//                   promises.push(this.lookup_mint_update_authority(account.data.parsed.info.mint
//               }
//           }
//
//           // Now await all results
//           await Promise.all(promises);
//
//           // After async call, check to make sure that rpc connection is still valid, and that the wallet pubkey
//           // holder has not changed to a new one
//           if (!this.rpc_connections.has(rpc_connection) || (wallet_pubkey_holder != this.wallet_pubkey_holder)) {
//               // Try again immediately
//               setTimeout(() => this.query_tokens(), 0);
//               return;
//           }
//
//           // For each token:
//           // - If it has bid_entry_mint_pubkey set, then it's a bid
//           // - Else if it has 
//           for (let token in tokens) {
//               
//           }
//           
//           
//           // For each wallet token, if it is a bid marker, then look up its bid entry
//           for (let idx = 0; idx < wallet_tokens.length; idx++) {
//               let wallet_token = wallet_tokens[idx];
//
//               if (wallet_token.mint_pubkey.equals(g_bid_marker_mint_pubkey)) {
//                   wallet_token.bid_entry_mint_pubkey =
//                       await this.lookup_bid_entry_mint_pubkey(wallet_token.account_pubkey);
//                   // After async call, check to make sure that rpc connection is still valid, and that the wallet
//                   // pubkey holder has not changed to a new one
//                   if (!this.rpc_connections.has(rpc_connection) ||
//                       (wallet_pubkey_holder != this.wallet_pubkey_holder)) {
//                       // Try again immediately
//                       setTimeout(() => this.query_tokens(), 0);
//                       return;
//                   }
//               }
//           }
//
//           on_wallet_tokens(wallet_tokens);
//                   
//           if (this.is_shutdown || (this.wallet_pubkey == null) || !wallet_pubkey.equals(this.wallet_pubkey)) {
//               return;
//           }
//                   
//           // Retry in 1 minute
//           if (repeat) {
//               setTimeout(() => this.query_tokens(wallet_pubkey, on_wallet_tokens), 60 * 1000);
//           }
//       }
//       catch(error) {
//           console.log("Query tokens error " + error);
//
//           if (this.is_shutdown || (this.wallet_pubkey == null) || !wallet_pubkey.equals(this.wallet_pubkey)) {
//               return;
//           }
//                   
//           // Retry in 1 second
//           setTimeout(() => this.query_tokens(wallet_pubkey, on_wallet_tokens), 1000);
//       }
//   }
//       catch (error) {
//           console.log("query_wallet error: " + error);
//
//           // Re-try wallet query after 1 second
//           setTimeout(() => this.query_wallet(), 1000);
//       }
//   }
// 
//   // maximum_price_lamports should be the price that was shown to the user
//   async buy_entry(entry, clock, maximum_price_lamports)
//   {
//       while (true) {
//           try {
//               let rpc_connection this.rpc_connections.get();
//
//               let wallet_holder = this.wallet_holder;
//
//               // If maximum_price_lamports was not passed in, then get it.  This maximum is
//               // guaranteed to be no higher than any value that was presented to them (since clock only moves
//               // forwards and thus entry price can only go down).
//               if (maximum_price_lamports == null) {
//                   maximum_price_lamports = entry.get_price(clock);
//               }
//
//               let admin_pubkey = await this.block.cluster.admin_pubkey(g_config_pubkey);
//
//               // If wallet pubkey changed, then abort
//               if (wallet_holder != this.wallet_holder) {
//                   break;
//               }
//
//               // If rpc connections changed, then try again
//               if (!this.rpc_connections.has(rpc_connection)) {
//                   continue;
//               }
//
//               let token_destination_pubkey = get_associated_token_pubkey(this.wallet_pubkey, entry.mint_pubkey);
//
//               let tx = _buy_tx({ funding_pubkey : this.wallet_pubkey,
//                                  config_pubkey : g_config_pubkey,
//                                  admin_pubkey : admin_pubkey,
//                                  block_pubkey : entry.block.pubkey,
//                                  entry_pubkey : entry.pubkey,
//                                  entry_token_pubkey : entry.token_pubkey,
//                                  entry_mint_pubkey : entry.mint_pubkey,
//                                  token_destination_pubkey : token_destination_pubkey,
//                                  token_destination_owner_pubkey : wallet_pubkey,
//                                  metaplex_metadata_pubkey : entry.metaplex_metadata_pubkey,
//                                  maximum_price_lamports : maximum_price_lamports });
//
//               return this.callbacks.submit_tx(tx);
//           }
//           catch (error) {
//               console.log("buy_entry error " + error);
//
//               // Try again after 5 seconds
//               return setTimeout(() => this.buy_entry(entry, clock, maximum_price_lamports));
//           }
//       }
//   }
//
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
exports.WalletItemType = WalletItemType;
exports.nifty_program_pubkey = () => g_nifty_program_pubkey;
exports.config_pubkey = () => g_config_pubkey;
exports.master_stake_pubkey = () => g_master_stake_pubkey;
exports.ki_mint_pubkey = () => g_ki_mint_pubkey;
exports.ki_metadata_pubkey = () => g_ki_metadata_pubkey;
exports.bid_marker_mint_pubkey = () => g_bid_marker_mint_pubkey;
