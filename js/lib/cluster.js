'use strict';

const SolanaWeb3 = require('@solana/web3.js');
const Buffer = require('buffer');
const _buy_tx = require("./tx.js")._buy_tx;
const _refund_tx = require("./tx.js")._refund_tx;
const _bid_tx = require("./tx.js")._bid_tx;
const _claim_losing_tx = require("./tx.js")._claim_losing_tx;
const _claim_winning_tx = require("./tx.js")._claim_winning_tx;
const _stake_tx = require("./tx.js")._stake_tx;
const _destake_tx = require("./tx.js")._destake_tx;
const _harvest_tx = require("./tx.js")._harvest_tx;
const _level_up_tx = require("./tx.js")._level_up_tx;
const _take_commission_or_delegate_tx = require("./tx.js")._take_commission_or_delegate_tx;


const NIFTY_PROGRAM_ADDRESS = "ShinboVZNAn1UjpZ3rJsFzLcWMP5JF8LPdHPWaaGYTV";
const METAPLEX_PROGRAM_ADDRESS = "metaqbxxUerdq28cj1RbAWkYQm3ybzjb6a8bt518x1s";
const SPL_TOKEN_PROGRAM_ADDRESS = "TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA";
const SPL_ASSOCIATED_TOKEN_PROGRAM_ADDRESS = "ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL";
const STAKE_PROGRAM_ADDRESS = "Stake11111111111111111111111111111111111111";

const BLOCKS_AT_ONCE = 5;
const ENTRIES_AT_ONCE = 25;
// Adjust this periodically to match what the cluster is achieving.  Could consider allowing this to be set on the
// Clock instance, and for some website somewhere to be scraped periodically to get the average periodically ...
const EXPECTED_SLOTS_PER_SECOND = 0.6;

const g_nifty_program_pubkey = new SolanaWeb3.PublicKey(NIFTY_PROGRAM_ADDRESS);
const g_metaplex_program_pubkey = new SolanaWeb3.PublicKey(METAPLEX_PROGRAM_ADDRESS);
const g_spl_token_program_pubkey = new SolanaWeb3.PublicKey(SPL_TOKEN_PROGRAM_ADDRESS);
const g_spl_associated_token_program_pubkey = new SolanaWeb3.PublicKey(SPL_ASSOCIATED_TOKEN_PROGRAM_ADDRESS);
const g_stake_program_pubkey = new SolanaWeb3.PublicKey(STAKE_PROGRAM_ADDRESS);

const g_config_pubkey = SolanaWeb3.PublicKey.findProgramAddressSync([ [ 1 ] ], g_nifty_program_pubkey)[0];

const g_master_stake_pubkey = SolanaWeb3.PublicKey.findProgramAddressSync([ [ 3 ] ], g_nifty_program_pubkey)[0];

const g_ki_mint_pubkey = SolanaWeb3.PublicKey.findProgramAddressSync([ [ 4 ] ], g_nifty_program_pubkey)[0];

const g_ki_metadata_pubkey = metaplex_metadata_pubkey(g_ki_mint_pubkey);


const WalletItemType = Object.freeze(
{
    Token : Symbol("Token"),

    Stake : Symbol("Stake")
});


class Cluster
{
    static nifty_program_pubkey()
    {
        return g_nifty_program_pubkey;
    }
    
    static config_pubkey()
    {
        return onfig_pubkey;
    }
    
    static master_stake_pubkey()
    {
        return g_master_stake_pubkey;
    }
    
    static ki_mint_pubkey()
    {
        return g_ki_mint_pubkey;
    }
    
    static ki_metadata_pubkey()
    {
        return g_ki_metadata_pubkey;
    }
    
    constructor(rpc_endpoint, on_block, on_entry)
    {
        this.rpc_connection = new SolanaWeb3.Connection(rpc_endpoint, "confirmed");
        this.on_block = on_block;
        this.on_entry = on_entry;

        // Defer loading blocks
        setTimeout(() => { this.#query_blocks(0, 200); }, 0);
    }

    set_wallet(wallet_pubkey, on_wallet_item)
    {
        if (on_wallet_item == null) {
            return;
        }
        
        this.wallet_pubkey = wallet_pubkey;

        this.on_wallet_item = on_wallet_item;

        // Initiate a query for all tokens belonging to wallet
        setTimeout(() => { this.#query_tokens(wallet_pubkey); }, 0);

        // Initiate a query for all stake accounts belonging to wallet
        setTimeout(() => { this.#query_stakes(wallet_pubkey); }, 0);
    }

    async get_admin_pubkey()
    {
        // Read it from the config account via RPC
        let result = await this.rpc_connection.getAccountInfo(g_config_pubkey,
                                                              {
                                                                  dataSlice : { offset : 4,
                                                                                length : 32 }
                                                              });
        if (result == null) {
            return null;
        }
        
        return new SolanaWeb3.PublicKey(result.data);
    }

    shutdown()
    {
        this.is_shutdown = true;
    }

    /**
     * Returns { confirmed_epoch, confirmed_slot, confirmed_unix_timestamp,
     *           slot, unix_timestamp }
     *
     * confirmed_epoch is the epoch at the most recently confirmed slot known to the client.
     * confirmed_slot is the most recently confirmed slot known to the client.
     * confirmed_unix_timestamp is the cluster timestamp of the most recently confirmed slot known to the client.
     * slot is an estimation of the current confirmed slot.
     * unix_timestamp is an estimation of the cluster timestamp of slot.
     *
     * slot and unix_timestamp MAY GO BACKWARDS in between subsequent calls, beware!
     **/
    async get_clock()
    {
        let now = (new Date()).getTime();
        
        // If the cluster info has never been queried, or it's been more than 5 seconds since it was last updated,
        // update it
        if ((this.confirmed_query_time == null) || (now > (this.confirmed_query_time + 5000))) {
            await this.#update_clock();
            now = (new Date()).getTime();
        }

        let slots_elapsed = ((now - this.confirmed_query_time) / (EXPECTED_SLOTS_PER_SECOND * 1000)) | 0; 

        return {
            confirmed_epoch : this.confirmed_epoch,
            confirmed_slot : this.confirmed_slot,
            confirmed_unix_timestamp : this.confirmed_unix_timestamp,
            slot : this.confirmed_slot + slots_elapsed,
            unix_timestamp : this.confirmed_unix_timestamp + (((now - this.confirmed_query_time) / 1000) | 0)
        };
    }

    #query_blocks(group_number, block_number)
    {
        if (this.is_shutdown) {
            return;
        }
        
        // Compute account keys
        let block_pubkeys = [ ];

        for (let i = 0; i < BLOCKS_AT_ONCE; i++) {
            block_pubkeys.push(get_block_pubkey(group_number, block_number + i));
        }

        this.rpc_connection.getMultipleAccountsInfo(block_pubkeys)
            .then((blocks) =>
                {
                    if (this.is_shutdown) {
                        return;
                    }
                    
                    let next_blocks = true;
                    let next_group = true;
                    
                    for (let idx = 0; idx < blocks.length; idx += 1) {
                        if (blocks[idx] == null) {
                            if (idx == 0) {
                                next_group = false;
                            }
                            next_blocks = false;
                            break;
                        }
                        this.on_block(new Block(this, block_pubkeys[idx], blocks[idx].data, true));
                        if (this.is_shutdown) {
                            return;
                        }
                    }
                    
                    if (next_blocks) {
                        this.#query_blocks(group_number, block_number + BLOCKS_AT_ONCE);
                    }
                    else if (next_group) {
                        this.#query_blocks(group_number + 1, 0);
                    }
                    
                    else if ((group_number == 0) && (block_number == 0)) {
                        // No blocks at all were found, so start over again in 5 seconds (because presumably there was
                        // an RPC error that prevented any blocks from being loaded, and we want to get past this
                        // error quickly).
                        setTimeout(() => { this.#query_blocks(0, 200); }, 5 * 1000);
                    }
                    else {
                        // Reached normal termination point -- the last available block was found -- so query again
                        // in 1 minute.
                        setTimeout(() => { this.#query_blocks(0, 200); }, 60 * 1000);
                    }
                })
            .catch(() =>
                {
                    if (this.is_shutdown) {
                        return;
                    }
                    
                    // Try again after 5 seconds
                    setTimeout(() => { this.#query_blocks(group_number, block_number); }, 5 * 1000);
                });
    }

    async #update_clock()
    {
        if (this.is_shutdown) {
            return;
        }

        let queried_epoch;
        let queried_slot;
        
        await this.rpc_connection.getEpochInfo()
            .then((epoch_info) =>
                {
                    if (this.is_shutdown) {
                        return;
                    }
                    
                    queried_epoch = epoch_info.epoch;
                    queried_slot = epoch_info.absoluteSlot;
                    
                    return this.rpc_connection.getBlockTime(queried_slot);
                })
            .then((time) =>
                {
                    if (this.is_shutdown) {
                        return;
                    }

                    // Update the current data, but only if it doesn't go backwards
                    this.confirmed_query_time = (new Date()).getTime();
                    this.confirmed_epoch = queried_epoch;
                    this.confirmed_slot = queried_slot;
                    this.confirmed_unix_timestamp = time;
                })
            .catch((error) =>
                {
                    if (this.is_shutdown) {
                        return;
                    }

                    return new Promise((resolve) => setTimeout(this.#update_clock, 1000));
                });
    }

    #query_tokens(wallet_pubkey)
    {
        if (this.is_shutdown || !wallet_pubkey.equals(this.wallet_pubkey)) {
            return;
        }

        this.rpc_connection.getParsedTokenAccountsByOwner(this.wallet_pubkey,
                                                          { programId : g_spl_token_program_pubkey })
            .then((results) =>
                {
                    if (this.is_shutdown || !wallet_pubkey.equals(this.wallet_pubkey)) {
                        return true;
                    }

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

                        this.on_wallet_item(
                            {
                                wallet_item_type : WalletItemType.Token,

                                owner_pubkey : wallet_pubkey,

                                mint_pubkey : new SolanaWeb3.PublicKey(account.data.parsed.info.mint),
                                
                                account_pubkey : pubkey,

                                amount : account.data.parsed.info.tokenAmount.amount
                            });
                    }

                    // Retry in 1 minute
                    setTimeout(() => { this.#query_tokens(wallet_pubkey); }, 60 * 1000);

                    return true;
                })
            .catch((error) =>
                {
                    if (this.is_shutdown) {
                        return;
                    }
                    
                    // Try again after 5 seconds
                    setTimeout(() => { this.#query_tokens(wallet_pubkey); }, 5 * 1000);
                });
    }

    #query_stakes(wallet_pubkey)
    {
        if (this.is_shutdown || !wallet_pubkey.equals(this.wallet_pubkey)) {
            return true;
        }

        // getProgramAccounts comparing the stake account withdraw authority with the wallet pubkey
        this.rpc_connection.getParsedProgramAccounts(g_stake_program_pubkey,
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
                                                     })
            .then((results) =>
                {
                    if (this.is_shutdown || !wallet_pubkey.equals(this.wallet_pubkey)) {
                        return true;
                    }

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
                        
                        let item = {
                            wallet_item_type : WalletItemType.Stake,

                            account_pubkey : pubkey,

                            withdraw_authority_pubkey : new SolanaWeb3.PublicKey(withdrawer)
                        };

                        if ((account.data.parsed.info.stake != null) &&
                            (account.data.parsed.info.stake.delegation != null)) {
                            let delegation = account.data.parsed.info.stake.delegation;
                            item.stake_lamports = BigInt(delegation.stake);
                            item.vote_account_pubkey = new SolanaWeb3.PublicKey(delegation.voter);
                        }

                        this.on_wallet_item(item);
                    }

                    // Retry in 1 minute
                    setTimeout(() => { this.#query_stakes(wallet_pubkey); }, 60 * 1000);

                    return true;
                })
            .catch((error) =>
                {
                    if (this.is_shutdown) {
                        return;
                    }
                    
                    // Try again after 5 seconds
                    setTimeout(() => { this.#query_stakes(wallet_pubkey); }, 5 * 1000);
                });
    }
}


class Block
{
    constructor(cluster, pubkey, data, load_entries)
    {
        this.cluster = cluster;
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
        this.block_start_timestamp = buffer_le_s64(data, 72);
        this.mysteries_sold_count = buffer_le_u16(data, 80);
        this.mystery_phase_end_timestamp = buffer_le_s64(data, 88);
        this.commission = buffer_le_u16(data, 96);
        this.last_commission_change_epoch = buffer_le_u64(data, 104);

        if (load_entries) {
            this.#query_entries(0);
        }
    }

    // Causes a query that will populate an up-to-date version of this Block and call back the onBlock call with it
    async refresh()
    {
        if (this.cluster.is_shutdown) {
            return;
        }
        
        await this.rpc_connection.getAccountInfo(this.pubkey)
            .then((block) =>
                {
                    if (this.cluster.is_shutdown) {
                        return;
                    }

                    this.cluster.on_block(new Block(this.cluster, this.pubkey, block.data, false));
                })
            .catch(() =>
                {
                    if (this.cluster.is_shutdown) {
                        return;
                    }
                    
                    // Try again after 5 seconds
                    setTimeout(() => { this.refresh(); }, 5 * 1000);
                });
    }

    // If a Block is not complete, it should never be shown to the user.  It should be hidden until it is complete.
    is_complete()
    {
        return this.added_entries_count == this.total_entry_count;
    }

    is_revealable(clock)
    {
        if (this.mysteries_sold_count == this.total_mystery_count) {
            return true;
        }
        
        let mystery_phase_end = block.block_start_timestamp + block.mystery_phase_duration;

        return (clock.unix_timestamp > mystery_phase_end);
    }

    #query_entries(entry_index)
    {
        // Compute account keys
        let entry_pubkeys = [ ];

        for (let i = 0; i < ENTRIES_AT_ONCE; i++) {
            entry_pubkeys.push(get_entry_pubkey(get_entry_mint_pubkey(this.pubkey, entry_index + i)));
        }

        this.cluster.rpc_connection.getMultipleAccountsInfo(entry_pubkeys)
            .then((entries) =>
                {
                    if (this.cluster.is_shutdown) {
                        return;
                    }
                    
                    let next_entries = true;
                    
                    for (let idx = 0; idx < entries.length; idx += 1) {
                        if (entries[idx] == null) {
                            next_entries = false;
                            break;
                        }
                        this.cluster.on_entry(new Entry(this, entry_pubkeys[idx], entries[idx].data));
                        if (this.cluster.is_shutdown) {
                            return;
                        }
                    }
                    
                    if (next_entries) {
                        this.#query_entries(entry_index + ENTRIES_AT_ONCE);
                    }
                })
            .catch(() =>
                {
                    if (!this.cluster.is_shutdown) {
                        // Try again after 5 seconds
                        setTimeout(() => { this.#query_entries(entry_index); }, 5 * 1000);
                    }
                });
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
        this.reveal_timestamp = buffer_le_s64(data, 200);
        this.purchase_price_lamports = buffer_le_u64(data, 208);
        this.refund_awarded = data[216];
        this.commission = buffer_le_u16(data, 218);
        this.auction_highest_bid_lamports = buffer_le_u64(data, 224);
        this.auction_winning_bid_pubkey = buffer_pubkey(data, 232);
        this.owned_stake_account = buffer_pubkey(data, 264);
        this.owned_stake_initial_lamports = buffer_le_u64(data, 296);
        this.owned_stake_epoch = buffer_le_u64(data, 304);
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

    // Causes a query that will populate an up-to-date version of this Entry and call back the onEntry call with it
    async refresh()
    {
        if (this.cluster.is_shutdown) {
            return;
        }
        
        await this.rpc_connection.getAccountInfo(this.pubkey)
            .then((entry) =>
                {
                    if (this.cluster.is_shutdown) {
                        return;
                    }

                    this.cluster.on_entry(new Entry(this.block, this.pubkey, entry.data));
                })
            .catch(() =>
                {
                    if (this.cluster.is_shutdown) {
                        return;
                    }
                    
                    // Try again after 5 seconds
                    setTimeout(() => { this.refresh(); }, 5 * 1000);
                });
    }
    
    // Cribbed from the nifty program's get_entry_state() function
    get_entry_state(clock)
    {
        if (this.reveal_sha256 == "11111111111111111111111111111111") {
            if (this.purchase_price_lamports > 0) {
                if (this.owned_stake_account == "11111111111111111111111111111111") {
                    return EntryState.Owned;
                }
                else {
                    return EntryState.OwnedAndStaked;
                }
            }
            else {
                if (this.has_auction) {
                    if ((this.reveal_timestamp + this.duration) > ((Date.now().getTime() / 1000) | 0)) {
                        return EntryState.InAuction;
                    }
                    else if (this.auction.highest_bid_lamports > 0) {
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

    // This only returns a valid value if the entry state is EntryState.PreRevealUnowned or EntryState.Unowned
    get_price(clock)
    {
        if (this.get_entry_state(this.block, clock) == EntryState.PreRevealUnowned) {
            return Entry.#compute_price(BigInt(this.block.mystery_phase_duration),
                                        BigInt(this.block.mystery_start_price_lamports),
                                        BigInt(this.block.minimum_price_lamports),
                                        BigInt(clock.unix_timestamp - this.block.block_start_timestamp));
        }
        else if (this.has_auction) {
            return this.block.minimum_price_lamports;
        }
        else {
            return Entry.#compute_price(BigInt(this.duration),
                                        BigInt(this.non_auction_start_price_lamports),
                                        BigInt(this.minimum_price_lamports),
                                        BigInt(clock.unix_timestamp - this.reveal_timestamp));
        }
    }

    // This only returns a valid value if the entry state is EntryState.InAuction
    get_auction_minimum_bid_lamports(clock)
    {
        return Entry.#compute_minimum_bid(BigInt(this.duration),
                                          BigInt(this.minimum_price_lamports),
                                          BigInt(this.auction_highest_bid_lamports),
                                          BigInt(clock.unix_timestamp - this.reveal_timestamp));
    }

    // All _tx functions require that the cluster of this Entry have a valid wallet pubkey set.
    async buy_tx()
    {
        let wallet_pubkey = this.block.cluster.wallet_pubkey;
        if (wallet_pubkey == null) {
            return null;
        }
        
        return _buy_tx({ funding_pubkey : wallet_pubkey,
                         config_pubkey : g_config_pubkey,
                         admin_pubkey : await this.block.cluster.get_admin_pubkey(g_config_pubkey),
                         block_pubkey : this.block.pubkey,
                         entry_pubkey : this.pubkey,
                         entry_token_pubkey : this.token_pubkey,
                         entry_mint_pubkey : this.mint_pubkey,
                         token_destination_pubkey : get_associated_token_pubkey(wallet_pubkey, this.mint_pubkey),
                         token_destination_owner_pubkey : wallet_pubkey,
                         metaplex_metadata_pubkey : this.metaplex_metadata_pubkey });
    }
    
    refund_tx()
    {
        let wallet_pubkey = this.block.cluster.wallet_pubkey;
        if (wallet_pubkey == null) {
            return null;
        }
        
        return _refund_tx({ token_owner_pubkey : wallet_pubkey,
                            block_pubkey : this.block.pubkey,
                            entry_pubkey : this.pubkey,
                            buyer_token_pubkey : get_associated_token_pubkey(wallet_pubkey, this.mint_pubkey),
                            refund_destination_pubkey : wallet_pubkey });
    }
    
    bid_tx(minimum_bid_lamports, maximum_bid_lamports)
    {
        let wallet_pubkey = this.block.cluster.wallet_pubkey;
        if ((wallet_pubkey == null) || (minimum_bid_lamports == null) || (maximum_bid_lamports == null)) {
            return null;
        }
        let bid_marker_token_pubkey = get_bid_marker_token_pubkey(this.mint_pubkey, wallet_pubkey);
            
        return _bid_tx({ bidding_pubkey : wallet_pubkey,
                         entry_pubkey : this.pubkey,
                         bid_marker_token_pubkey : bid_marker_token_pubkey,
                         bid_pubkey : get_bid_pubkey(bid_marker_token_pubkey),
                         minimum_bid_lamports : minimum_bid_lamports,
                         maximum_bid_lamports : maximum_bid_lamports });
    }
    
    claim_losing_tx()
    {
        let wallet_pubkey = this.block.cluster.wallet_pubkey;
        if (wallet_pubkey == null) {
            return null;
        }
        let bid_marker_token_pubkey = get_bid_marker_token_pubkey(this.mint_pubkey, wallet_pubkey);
            
        return _claim_losing_tx({ bidding_pubkey : wallet_pubkey,
                                  entry_pubkey : this.pubkey,
                                  bid_pubkey : get_bid_pubkey(bid_marker_token_pubkey),
                                  bid_marker_token_pubkey : bid_marker_token_pubkey });
    }
    
    async claim_winning_tx()
    {
        let wallet_pubkey = this.block.cluster.wallet_pubkey;
        if (wallet_pubkey == null) {
            return null;
        }
        let token_destination_pubkey = get_associated_token_pubkey(wallet_pubkey, this.mint_pubkey);
        let bid_marker_token_pubkey = get_bid_marker_token_pubkey(this.mint_pubkey, wallet_pubkey);
        
        return _claim_winning_tx({ bidding_pubkey : wallet_pubkey,
                                   entry_pubkey : this.pubkey,
                                   bid_pubkey : get_bid_pubkey(bid_marker_token_pubkey),
                                   config_pubkey : g_config_pubkey,
                                   admin_pubkey : await this.block.cluster.get_admin_pubkey(g_config_pubkey),
                                   entry_token_pubkey : this.token_pubkey,
                                   entry_mint_pubkey : this.mint_pubkey,
                                   token_destination_pubkey : token_destination_pubkey,
                                   token_destination_owner_pubkey : wallet_pubkey,
                                   bid_marker_token_pubkey : bid_marker_token_pubkey });
    }
    
    stake_tx(stake_account_pubkey)
    {
        let wallet_pubkey = this.block.cluster.wallet_pubkey;
        if ((wallet_pubkey == null) || (stake_account_pubkey == null)) {
            return null;
        }
        
        return _stake_tx({ block_pubkey : this.block.pubkey,
                           entry_pubkey : this.pubkey,
                           token_owner_pubkey : wallet_pubkey,
                           token_pubkey : get_associated_token_pubkey(wallet_pubkey, this.mint_pubkey),
                           stake_pubkey : stake_account_pubkey,
                           withdraw_authority : wallet_pubkey });
    }
    
    destake_tx()
    {
        console.log("1");
        let wallet_pubkey = this.block.cluster.wallet_pubkey;
        if (wallet_pubkey == null) {
            return null;
        }
        
        return _destake_tx({ funding_pubkey : wallet_pubkey,
                             block_pubkey : this.block.pubkey,
                             entry_pubkey : this.pubkey,
                             token_owner_pubkey : wallet_pubkey,
                             token_pubkey : get_associated_token_pubkey(wallet_pubkey, this.mint_pubkey),
                             stake_pubkey : this.owned_stake_account,
                             ki_destination_pubkey : get_associated_token_pubkey(wallet_pubkey, g_ki_mint_pubkey),
                             ki_destination_owner_pubkey : wallet_pubkey,
                             bridge_pubkey : get_entry_bridge_pubkey(this.mint_pubkey),
                             new_withdraw_authority : wallet_pubkey.toBase58() });
    }
    
    harvest_tx()
    {
        let wallet_pubkey = this.block.cluster.wallet_pubkey;
        if (wallet_pubkey == null) {
            return null;
        }
        
        return _harvest_tx({ funding_pubkey : wallet_pubkey,
                             entry_pubkey : this.pubkey,
                             token_owner_pubkey : wallet_pubkey,
                             token_pubkey : get_associated_token_pubkey(wallet_pubkey, this.mint_pubkey),
                             stake_pubkey : this.owned_stake_account,
                             ki_destination_pubkey : get_associated_token_pubkey(wallet_pubkey, g_ki_mint_pubkey),
                             ki_destination_owner_pubkey : wallet_pubkey });
    }
    
    level_up_tx()
    {
        let wallet_pubkey = this.block.cluster.wallet_pubkey;
        if (wallet_pubkey == null) {
            return null;
        }
        
        return _level_up_tx({ entry_pubkey : this.pubkey,
                              token_owner_pubkey : wallet_pubkey,
                              token_pubkey : get_associated_token_pubkey(wallet_pubkey, this.mint_pubkey),
                              entry_metaplex_metadata_pubkey : this.metaplex_metadata_pubkey,
                              ki_source_pubkey : get_associated_token_pubkey(wallet_pubkey, g_ki_mint_pubkey),
                              ki_source_owner_pubkey : wallet_pubkey });
    }
    
    take_commission_or_delegate_tx()
    {
        let wallet_pubkey = this.block.cluster.wallet_pubkey;
        if (wallet_pubkey == null) {
            return null;
        }
        
        return _take_commission_or_delegate_tx({ funding_pubkey : wallet_pubkey,
                                                 block_pubkey : this.block.pubkey,
                                                 entry_pubkey : this.pubkey,
                                                 stake_pubkey : this.owned_stake_account,
                                                 bridge_pubkey : get_entry_bridge_pubkey(this.mint_pubkey) });
    }

    // Cribbed from user_buy.c
    static #compute_price(total_seconds, start_price, end_price, seconds_elapsed)
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
    static #compute_minimum_bid(auction_duration, initial_minimum_bid, current_max_bid, seconds_elapsed)
    {
        if (current_max_bid < initial_minimum_bid) {
            return initial_minimum_bid;
        }

        if (seconds_elapsed > auction_duration) {
            seconds_elapsed = auction_duration;
        }

        let a = seconds_elapsed;
        let b = auction_duration;
        let p = current_max_bid;

        let result = (p * (((1000n * b) / ((b + (b / 100n)) - a)) + 109000n)) / 100000n;

        let min_result = current_max_bid + (current_max_bid / 10n);
        let max_result = (2n * current_max_bid) + ((current_max_bid * 9n) / 100n);
    
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


exports.Cluster = Cluster;
exports.Block = Block;
exports.Entry = Entry;
