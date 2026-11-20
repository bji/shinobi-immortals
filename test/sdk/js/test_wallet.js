'use strict';

// Tests the transactions of sdk/js/shinobi_immortals.js, against a mock RPC server (see make_mock_rpc in common.js):
//
// - Each transaction method of Wallet sends a transaction holding exactly the instruction that the corresponding
//   builder of shinobi_immortals_base.js makes, with the wallet as fee payer, validly signed by the wallet's signer
// - create_callback_signer: a signature obtained by a callback is used, including one made by solxact from the base64
//   transaction given to the callback, just as a person would with their own tools; and an invalid signature, or a
//   callback that declines to sign, sends nothing
// - A blockhash that expires before the transaction is signed aborts the signing, and the transaction is built and
//   signed again with a new blockhash
// - A signer that changes the transaction, or a signer that is not the wallet's, sends nothing
// - Without WebCrypto, a transaction signed through a callback signer is still verified and sent
//
// This needs @solana/kit from npm (see package.json), and solxact; it is skipped if @solana/kit is not installed.

const crypto = require("crypto");
const fs = require("fs");
const os = require("os");
const path = require("path");
const { execFileSync } = require("child_process");
const { SDK_DIR, Base, Results, random_bytes, random_address, require_tools, use_packages, transaction_permissions,
        make_mock_rpc, make_mock_rpc_connections, make_key_pair, to_json } = require("./common.js");

const results = new Results("wallet");

if (!use_packages(results.name)) {
    process.exit(0);
}

require_tools(results.name, [ "sh", "base64", "solxact" ]);

const Kit = require("@solana/kit");
const SDK = require(path.join(SDK_DIR, "shinobi_immortals.js"));


// The mock cluster ---------------------------------------------------------------------------------------------------

// The admin, whose address the mock cluster's program state account holds
const ADMIN = random_address();

// Returns a mock cluster, as { cluster, sent, blockhashes, set_block_height }: sent records each transaction sent (as
// its wire bytes), blockhashes each blockhash given out, and set_block_height sets the cluster's block height.  Each
// blockhash given out is valid through block height 100.  If send_error is given, sending a transaction fails with
// that JSON-RPC error (after it is recorded in sent).
function make_cluster(send_error)
{
    let sent = [ ];
    let blockhashes = [ ];
    let block_height = 50;

    // The program state account: its data type, then the admin's address
    let program_state = new Uint8Array(36);
    program_state[0] = 1;
    program_state.set(Base.address_to_bytes(ADMIN), 4);

    let { rpc } = make_mock_rpc({
        getLatestBlockhash : () => {
            let blockhash = random_address();
            blockhashes.push(blockhash);
            return { context : { slot : 1 }, value : { blockhash : blockhash, lastValidBlockHeight : 100 } };
        },
        getBlockHeight : () => block_height,
        getAccountInfo : ([ address ]) => {
            if (address != Base.g_program_state_address) {
                throw new Error("unexpected account " + address);
            }
            return { context : { slot : 1 },
                     value : { data : [ Buffer.from(program_state).toString("base64"), "base64" ], executable : false,
                               lamports : 1000000, owner : Base.g_self_program_address, rentEpoch : 0, space : 36 } };
        },
        sendTransaction : ([ wire_base64 ]) => {
            let wire = Buffer.from(wire_base64, "base64");
            sent.push(wire);
            if (send_error != null) {
                throw { rpc_error : send_error };
            }
            // The signature of a transaction is its fee payer's (first) signature, after the signature count byte
            return Base.base58_encode(wire.subarray(1, 65));
        }
    });

    return {
        cluster : { rpc_connections : make_mock_rpc_connections(SDK, rpc) },
        sent : sent,
        blockhashes : blockhashes,
        set_block_height : (height) => { block_height = height; }
    };
}


// Returns a Wallet of a mock cluster for wallet_address, which checks for an expired blockhash often, so that the
// tests of expiry are quick
function make_wallet(mock, wallet_address)
{
    let wallet = SDK.Wallet.create(mock.cluster, wallet_address);
    wallet.expiry_check_interval_ms = 10;
    return wallet;
}


// Returns { fee_payer, instruction, signatures_valid, signature } of a wire transaction: its fee payer, its single
// instruction (in the form that transaction_permissions gives), whether all of its signatures are valid, and its
// signature
async function decode_sent(wire)
{
    let transaction = Kit.getTransactionDecoder().decode(wire);
    let message = Kit.decompileTransactionMessage(
        Kit.getCompiledTransactionMessageDecoder().decode(transaction.messageBytes));
    results.check("a sent transaction holds one instruction", message.instructions.length == 1);
    let instruction = message.instructions[0];

    let signatures_valid = true;
    for (let [ address, signature ] of Object.entries(transaction.signatures)) {
        signatures_valid = signatures_valid && (signature != null) &&
            await Kit.verifySignature(await Kit.getPublicKeyFromAddress(address), signature, transaction.messageBytes);
    }

    return {
        fee_payer : message.feePayer.address,
        instruction : {
            program : instruction.programAddress,
            accounts : instruction.accounts.map((a) => [ a.address, (a.role & 2) != 0, (a.role & 1) != 0 ]),
            data : Base.bytes_to_hex(instruction.data)
        },
        signatures_valid : signatures_valid,
        signature : Kit.getSignatureFromTransaction(transaction)
    };
}


// Returns an instruction in the form that decode_sent gives, with the permissions that a transaction with fee_payer
// gives its accounts
function expected_instruction(instruction, fee_payer)
{
    return {
        program : instruction.programAddress,
        accounts : transaction_permissions(instruction, fee_payer),
        data : Base.bytes_to_hex(instruction.data)
    };
}


// Returns the result of promise, or throws if it takes more than 10 seconds (as it would if, for example, a signer
// were left waiting by an expiry that was never noticed)
function within_time_limit(description, promise)
{
    let timer;
    let timeout = new Promise((resolve, reject) => {
        timer = setTimeout(() => reject(new Error(description + " did not finish within 10 seconds")), 10000);
    });
    return Promise.race([ promise, timeout ]).finally(() => clearTimeout(timer));
}


// Expects call to throw an error including message_part, having sent nothing on mock
async function check_rejects(description, mock, call, message_part)
{
    try {
        await call();
        results.check(description, false, "did not throw");
    }
    catch (error) {
        results.check(description, error.message.includes(message_part),
                      "threw \"" + error.message + "\", expected a message including \"" + message_part + "\"");
    }
    results.check(description + ": nothing is sent", mock.sent.length == 0);
}


async function main()
{
    // A block and an entry, decoded by the Block and Entry classes from random account data, which also checks
    // that the classes decode as the base decoders do
    let block_data = random_bytes(104);
    let entry_data = random_bytes(3200);
    entry_data[304] = 3;
    let block = new SDK.Block(null, random_address(), block_data);
    let entry = new SDK.Entry(block, random_address(), entry_data);
    let decoded_block = Base.decode_block(block_data);
    let decoded_entry = Base.decode_entry(entry_data);
    results.check("Block holds the fields of decode_block",
                  Object.keys(decoded_block).every((k) => to_json(block[k]) == to_json(decoded_block[k])));
    results.check("Entry holds the fields of decode_entry",
                  Object.keys(decoded_entry).every((k) => to_json(entry[k]) == to_json(decoded_entry[k])));

    // The wallets' key pairs, with @solana/kit signers for them
    let user_key_pair = make_key_pair();
    let other_key_pair = make_key_pair();
    let admin_signer = await Kit.generateKeyPairSigner();
    let user_signer = await Kit.createKeyPairSignerFromBytes(user_key_pair.bytes);
    let other_signer = await Kit.createKeyPairSignerFromBytes(other_key_pair.bytes);
    results.check("a key pair's @solana/kit signer has its address", user_signer.address == user_key_pair.address);
    let user = user_signer.address;
    let other = other_signer.address;
    let stake = random_address();
    const T = Base.WithdrawExcessAccountType;


    // Each transaction method ----------------------------------------------------------------------------------------

    // [ description, signer, call(wallet, signer), the builder's instruction ]; the withdraw_excess methods are the
    // admin's, so their signer is the admin's, whose address the mock program state must hold
    let admin_mock_address = admin_signer.address;
    let methods = [
        [ "buy_entry", user_signer, (w, s) => w.buy_entry(entry, 123456789n, s),
          Base.make_buy_instruction(entry, 123456789n, user, ADMIN) ],
        [ "refund_entry", user_signer, (w, s) => w.refund_entry(entry, s), Base.make_refund_instruction(entry, user) ],
        [ "bid_on_entry", user_signer, (w, s) => w.bid_on_entry(entry, 1000n, 2000n, s),
          Base.make_bid_instruction(entry, 1000n, 2000n, user) ],
        [ "claim_entry (won)", user_signer, (w, s) => w.claim_entry(entry, true, s),
          Base.make_settle_winning_bid_instruction(entry, user, ADMIN, user) ],
        [ "claim_entry (lost)", user_signer, (w, s) => w.claim_entry(entry, false, s),
          Base.make_claim_losing_instruction(entry, user) ],
        [ "stake_entry", user_signer, (w, s) => w.stake_entry(entry, stake, s),
          Base.make_stake_instruction(entry, stake, user) ],
        [ "destake_entry", user_signer, (w, s) => w.destake_entry(entry, s),
          Base.make_destake_instruction(entry, user, false, false) ],
        [ "destake_entry (no_ki_harvest)", user_signer, (w, s) => w.destake_entry(entry, s, true, false),
          Base.make_destake_instruction(entry, user, true, false) ],
        [ "destake_entry (no_thaw)", user_signer, (w, s) => w.destake_entry(entry, s, false, true),
          Base.make_destake_instruction(entry, user, false, true) ],
        [ "thaw_entry", user_signer, (w, s) => w.thaw_entry(entry, s), Base.make_thaw_instruction(entry, user) ],
        [ "harvest_entry", user_signer, (w, s) => w.harvest_entry(entry, s),
          Base.make_harvest_instruction(entry, user) ],
        [ "level_up_entry", user_signer, (w, s) => w.level_up_entry(entry, "18446744073709551615", s),
          Base.make_level_up_instruction(entry, "18446744073709551615", user) ],
        [ "settle_winning_bid", other_signer, (w, s) => w.settle_winning_bid(entry, user, s),
          Base.make_settle_winning_bid_instruction(entry, user, ADMIN, other) ],
        [ "delegate", other_signer, (w, s) => w.delegate(entry, s), Base.make_delegate_instruction(entry) ],
        [ "update_epochs_staked", other_signer, (w, s) => w.update_epochs_staked(entry, s),
          Base.make_update_epochs_staked_instruction(entry) ],
        [ "withdraw_excess (program state)", admin_signer, (w, s) => w.withdraw_excess(T.ProgramState, null, s),
          Base.make_withdraw_excess_instruction(T.ProgramState, null, admin_mock_address) ],
        [ "withdraw_excess (authority)", admin_signer, (w, s) => w.withdraw_excess(T.Authority, null, s),
          Base.make_withdraw_excess_instruction(T.Authority, null, admin_mock_address) ],
        [ "withdraw_excess (block)", admin_signer, (w, s) => w.withdraw_excess(T.Block, block, s),
          Base.make_withdraw_excess_instruction(T.Block, block, admin_mock_address) ],
        [ "withdraw_excess (entry)", admin_signer, (w, s) => w.withdraw_excess(T.Entry, entry, s),
          Base.make_withdraw_excess_instruction(T.Entry, entry, admin_mock_address) ],
        [ "withdraw_excess (whitelist)", admin_signer, (w, s) => w.withdraw_excess(T.Whitelist, block, s),
          Base.make_withdraw_excess_instruction(T.Whitelist, block, admin_mock_address) ]
    ];

    for (let [ description, signer, call, instruction ] of methods) {
        let mock = make_cluster();
        let signature;
        try {
            signature = await call(make_wallet(mock, signer.address), signer);
        }
        catch (error) {
            results.check(description, false, "threw " + error.message);
            continue;
        }
        if (!results.check(description + ": one transaction is sent", mock.sent.length == 1)) {
            continue;
        }
        let decoded = await decode_sent(mock.sent[0]);
        results.check(description + ": the wallet pays the fee", decoded.fee_payer == signer.address);
        results.check(description + ": the instruction is the builder's",
                      to_json(decoded.instruction) == to_json(expected_instruction(instruction, signer.address)),
                      "sent " + to_json(decoded.instruction).substring(0, 300));
        results.check(description + ": the signatures are valid", decoded.signatures_valid);
        results.check(description + ": the transaction's signature is returned", signature == decoded.signature);
    }


    // create_callback_signer -----------------------------------------------------------------------------------------

    // A key file for solxact, for the user's key pair
    let key_dir = fs.mkdtempSync(path.join(os.tmpdir(), "shinobi-sdk-js-wallet-"));
    let key_file = path.join(key_dir, "user.json");
    fs.writeFileSync(key_file, JSON.stringify([ ...user_key_pair.bytes ]));

    try {
        // The callback gets the transaction as base64, which a person would decode and sign with their own tools;
        // here, solxact signs it, and prints its signature (base58), which the callback returns
        let mock = make_cluster();
        let offered = [ ];
        let solxact_signer = SDK.create_callback_signer(user, async (transaction_base64, abort_signal) => {
            offered.push(transaction_base64);
            return execFileSync("sh", [ "-c", "base64 -d | solxact sign \"$0\" | solxact signature", key_file ],
                                { input : transaction_base64 }).toString();
        });
        let signature = await make_wallet(mock, user).thaw_entry(entry, solxact_signer);
        results.check("callback signer: the callback is given the transaction once", offered.length == 1);
        if (mock.sent.length == 1) {
            let decoded = await decode_sent(mock.sent[0]);
            results.check("callback signer: a signature made by solxact is used", decoded.signatures_valid &&
                          (signature == decoded.signature));
            results.check("callback signer: the transaction is the builder's",
                          to_json(decoded.instruction) ==
                          to_json(expected_instruction(Base.make_thaw_instruction(entry, user), user)));
            // What solxact decodes from the base64 given to the callback is that transaction
            let solxact_view = JSON.parse(execFileSync("sh", [ "-c", "base64 -d | solxact decode" ],
                                                       { input : offered[0] }).toString());
            results.check("callback signer: solxact decodes the transaction given to the callback",
                          (solxact_view.instructions.length == 1) &&
                          (Base.bytes_to_hex(solxact_view.instructions[0].data) == decoded.instruction.data) &&
                          (solxact_view.addresses.find((a) => a.fee_payer).address == user));
        }
        else {
            results.check("callback signer: one transaction is sent", false, mock.sent.length + " sent");
        }

        // A callback may return the signature as bytes
        mock = make_cluster();
        let bytes_signer = SDK.create_callback_signer(user, async (transaction_base64) => {
            let transaction = Kit.getTransactionDecoder().decode(Buffer.from(transaction_base64, "base64"));
            return (await user_signer.signTransactions([ transaction ]))[0][user];
        });
        await make_wallet(mock, user).thaw_entry(entry, bytes_signer);
        results.check("callback signer: a signature returned as bytes is used",
                      (mock.sent.length == 1) && (await decode_sent(mock.sent[0])).signatures_valid);

        // A signature by another key is rejected
        mock = make_cluster();
        let wrong_signer = SDK.create_callback_signer(user, async (transaction_base64) => {
            let transaction = Kit.getTransactionDecoder().decode(Buffer.from(transaction_base64, "base64"));
            let signature = await Kit.signBytes(other_signer.keyPair.privateKey, transaction.messageBytes);
            return Base.base58_encode(signature);
        });
        await check_rejects("callback signer: a signature by another key is rejected", mock,
                            () => make_wallet(mock, user).thaw_entry(entry, wrong_signer), "is not");

        // ... and does so itself, when used directly with @solana/kit, without Wallet
        let direct_message = Kit.pipe(
            Kit.createTransactionMessage({ version : "legacy" }),
            (m) => Kit.setTransactionMessageFeePayer(Kit.address(user), m),
            (m) => Kit.setTransactionMessageLifetimeUsingBlockhash({ blockhash : Kit.blockhash(random_address()),
                                                                     lastValidBlockHeight : 100n }, m),
            (m) => Kit.appendTransactionMessageInstruction(Base.make_thaw_instruction(entry, user), m));
        try {
            await wrong_signer.signTransactions([ Kit.compileTransaction(direct_message) ]);
            results.check("callback signer: rejects a signature by another key by itself", false, "did not throw");
        }
        catch (error) {
            results.check("callback signer: rejects a signature by another key by itself",
                          error.message.includes("is not " + user + "'s signature"), error.message);
        }

        // A signature of another transaction is rejected
        mock = make_cluster();
        let other_transaction_signer = SDK.create_callback_signer(user, async () => {
            return Base.base58_encode(await Kit.signBytes(user_signer.keyPair.privateKey, random_bytes(100)));
        });
        await check_rejects("callback signer: a signature of something else is rejected", mock,
                            () => make_wallet(mock, user).thaw_entry(entry, other_transaction_signer), "is not");

        // Something that is not a signature is rejected
        mock = make_cluster();
        let short_signer = SDK.create_callback_signer(user, async () => Base.base58_encode(random_bytes(63)));
        await check_rejects("callback signer: a 63 byte signature is rejected", mock,
                            () => make_wallet(mock, user).thaw_entry(entry, short_signer), "must be 64 bytes");

        // A callback declines to sign by throwing
        mock = make_cluster();
        let declining_signer = SDK.create_callback_signer(user, async () => {
            throw new Error("declined by the user");
        });
        await check_rejects("callback signer: declining to sign sends nothing", mock,
                            () => make_wallet(mock, user).thaw_entry(entry, declining_signer), "declined by the user");
    }
    finally {
        fs.rmSync(key_dir, { recursive : true, force : true });
    }


    // Expiry ---------------------------------------------------------------------------------------------------------

    // The first request for a signature waits until it is aborted, which happens when the block height passes the
    // blockhash's last valid block height; the transaction is then built again, with a new blockhash, and signed
    {
        let mock = make_cluster();
        let calls = [ ];
        let signer = SDK.create_callback_signer(user, async (transaction_base64, abort_signal) => {
            calls.push({ transaction_base64, abort_signal });
            if (calls.length == 1) {
                // The blockhash expires while this waits for a person to sign
                mock.set_block_height(101);
                return new Promise((resolve, reject) => {
                    abort_signal.addEventListener("abort", () => reject(abort_signal.reason));
                });
            }
            mock.set_block_height(60);
            let transaction = Kit.getTransactionDecoder().decode(Buffer.from(transaction_base64, "base64"));
            return (await user_signer.signTransactions([ transaction ]))[0][user];
        });
        try {
            await within_time_limit("signing after an expiry", make_wallet(mock, user).thaw_entry(entry, signer));
        }
        catch (error) {
            results.check("expiry: signing finishes", false, error.message);
        }
        results.check("expiry: the signer is asked again after its blockhash expires", calls.length == 2);
        results.check("expiry: the first request is aborted", (calls.length == 2) && calls[0].abort_signal.aborted);
        results.check("expiry: the second request has a new blockhash",
                      (mock.blockhashes.length == 2) && (mock.blockhashes[0] != mock.blockhashes[1]) &&
                      (calls.length == 2) && (calls[0].transaction_base64 != calls[1].transaction_base64));
        results.check("expiry: only the transaction with the new blockhash is sent",
                      (mock.sent.length == 1) &&
                      (Kit.getCompiledTransactionMessageDecoder().decode(
                          Kit.getTransactionDecoder().decode(mock.sent[0]).messageBytes).lifetimeToken ==
                       mock.blockhashes[1]));
    }

    // A signer that ignores the abort, and signs after the blockhash has expired, is asked again too
    {
        let mock = make_cluster();
        let count = 0;
        let signer = SDK.create_callback_signer(user, async (transaction_base64) => {
            count += 1;
            if (count == 1) {
                mock.set_block_height(101);
                await new Promise((resolve) => setTimeout(resolve, 100));
            }
            else {
                mock.set_block_height(60);
            }
            let transaction = Kit.getTransactionDecoder().decode(Buffer.from(transaction_base64, "base64"));
            return (await user_signer.signTransactions([ transaction ]))[0][user];
        });
        await make_wallet(mock, user).thaw_entry(entry, signer);
        results.check("expiry: a signature made after the blockhash expired is not sent",
                      (count == 2) && (mock.sent.length == 1) &&
                      (Kit.getCompiledTransactionMessageDecoder().decode(
                          Kit.getTransactionDecoder().decode(mock.sent[0]).messageBytes).lifetimeToken ==
                       mock.blockhashes[1]));
    }


    // Signers that cannot be used ------------------------------------------------------------------------------------

    // A modifying signer may sign, but not change, the transaction
    {
        let mock = make_cluster();
        let unchanged_signer = Object.freeze({
            address : user_signer.address,
            async modifyAndSignTransactions(transactions) {
                return Promise.all(transactions.map(async (t) => {
                    let signature = (await user_signer.signTransactions([ t ]))[0];
                    return Object.freeze({ ...t, signatures : Object.freeze({ ...t.signatures, ...signature }) });
                }));
            }
        });
        await make_wallet(mock, user).thaw_entry(entry, unchanged_signer);
        results.check("a modifying signer that does not change the transaction may sign it", mock.sent.length == 1);

        mock = make_cluster();
        let changing_signer = Object.freeze({
            address : user_signer.address,
            async modifyAndSignTransactions(transactions) {
                return Promise.all(transactions.map(async (t) => {
                    // Change the last byte of the message (the instruction data), and sign the changed message
                    let changed = Uint8Array.from(t.messageBytes);
                    changed[changed.length - 1] ^= 1;
                    let signature = await Kit.signBytes(user_signer.keyPair.privateKey, changed);
                    return Object.freeze({ messageBytes : changed,
                                           signatures : Object.freeze({ [user] : signature }) });
                }));
            }
        });
        await check_rejects("a signer that changes the transaction is refused", mock,
                            () => make_wallet(mock, user).thaw_entry(entry, changing_signer),
                            "changed the transaction");
    }

    {
        let mock = make_cluster();
        let invalid_signer = Object.freeze({
            address : user,
            async signTransactions(transactions) {
                return transactions.map(() => Object.freeze({ [user] : random_bytes(64) }));
            }
        });
        await check_rejects("a signer that returns an invalid signature is refused", mock,
                            () => make_wallet(mock, user).thaw_entry(entry, invalid_signer), "is not valid");
    }

    {
        let mock = make_cluster();
        await check_rejects("a signer that is not the wallet's is refused", mock,
                            () => make_wallet(mock, user).thaw_entry(entry, other_signer),
                            "is not the wallet's address");
        await check_rejects("no signer is refused", mock, () => make_wallet(mock, user).thaw_entry(entry, null),
                            "A signer is required");
        let sending_signer = Object.freeze({ address : user, async signAndSendTransactions() { return [ ]; } });
        await check_rejects("a sending signer is refused", mock,
                            () => make_wallet(mock, user).thaw_entry(entry, sending_signer),
                            "TransactionPartialSigner");
    }

    // A transaction that the RPC server's simulation rejects is reported with the reason and the programs' logs, and
    // not sent again.  This is the error that an RPC server gives, as a real one gave it for a transfer that left an
    // account with too little for rent.
    {
        let mock = make_cluster({
            code : -32002,
            message : "Transaction simulation failed: Transaction results in an account (1) with insufficient funds " +
                "for rent",
            data : { accounts : null, err : { InsufficientFundsForRent : { account_index : 1 } },
                     innerInstructions : null,
                     logs : [ "Program 11111111111111111111111111111111 invoke [1]",
                              "Program 11111111111111111111111111111111 success" ],
                     replacementBlockhash : null, returnData : null, unitsConsumed : 150 }
        });
        try {
            await make_wallet(mock, user).thaw_entry(entry, user_signer);
            results.check("a failed simulation is reported", false, "did not throw");
        }
        catch (error) {
            results.check("a failed simulation is reported with its reason",
                          error.message.includes("insufficient funds for rent"), error.message);
            results.check("a failed simulation is reported with the programs' logs",
                          error.message.includes("Program 11111111111111111111111111111111 success"), error.message);
        }
        results.check("a failed simulation is not retried", mock.sent.length == 1, mock.sent.length + " sends");
    }

    // The wallet's address changing while a transaction is being signed abandons it
    {
        let mock = make_cluster();
        let wallet = make_wallet(mock, user);
        let changing_wallet_signer = SDK.create_callback_signer(user, async (transaction_base64) => {
            wallet.set_wallet_address(other);
            let transaction = Kit.getTransactionDecoder().decode(Buffer.from(transaction_base64, "base64"));
            return (await user_signer.signTransactions([ transaction ]))[0][user];
        });
        await check_rejects("a change of the wallet's address while signing abandons the transaction", mock,
                            () => wallet.thaw_entry(entry, changing_wallet_signer), "Wallet change");
    }

    // Without WebCrypto -----------------------------------------------------------------------------------------------

    // shinobi_immortals.js verifies signatures itself (with ed25519_verify of shinobi_immortals_base.js), so that it
    // does not need Ed25519 support in the Javascript runtime's WebCrypto.  With every WebCrypto operation made to
    // fail, a transaction signed by solxact through a callback signer must still be checked and sent, and an invalid
    // signature must still be caught.  This is done last, as the other tests use @solana/kit's key pair signers,
    // which do need WebCrypto.
    {
        let subtle = globalThis.crypto.subtle;
        let disabled = [ ];
        for (let name of [ "importKey", "exportKey", "generateKey", "sign", "verify", "digest" ]) {
            let original = subtle[name];
            subtle[name] = () => { throw new Error("WebCrypto is disabled by this test"); };
            disabled.push([ name, original ]);
        }

        // Make sure that WebCrypto really is unusable: @solana/kit itself cannot verify a signature
        let kit_failed = false;
        try {
            await Kit.getPublicKeyFromAddress(user);
        }
        catch (error) {
            kit_failed = true;
        }
        results.check("without WebCrypto: @solana/kit cannot use WebCrypto", kit_failed);

        let key_dir = fs.mkdtempSync(path.join(os.tmpdir(), "shinobi-sdk-js-wallet-"));
        let key_file = path.join(key_dir, "user.json");
        fs.writeFileSync(key_file, JSON.stringify([ ...user_key_pair.bytes ]));
        try {
            let mock = make_cluster();
            let solxact_signer = SDK.create_callback_signer(user, async (transaction_base64) => {
                return execFileSync("sh", [ "-c", "base64 -d | solxact sign \"$0\" | solxact signature", key_file ],
                                    { input : transaction_base64 }).toString();
            });
            let signature;
            try {
                signature = await make_wallet(mock, user).thaw_entry(entry, solxact_signer);
            }
            catch (error) {
                results.check("without WebCrypto: thaw_entry succeeds", false, "threw \"" + error.message + "\"");
            }
            let sent_signature = (mock.sent.length == 1) ? Base.base58_encode(mock.sent[0].subarray(1, 65)) : null;
            results.check("without WebCrypto: a transaction signed by solxact is sent",
                          (mock.sent.length == 1) && (signature == sent_signature), mock.sent.length + " sent");
            // The signature that was sent is valid (checked with Node's own ed25519, which is not WebCrypto)
            if (mock.sent.length == 1) {
                let transaction = Kit.getTransactionDecoder().decode(mock.sent[0]);
                results.check("without WebCrypto: the signature sent is valid",
                              crypto.verify(null, Buffer.from(transaction.messageBytes),
                                            crypto.createPublicKey({
                                                key : Buffer.concat([ Buffer.from("302a300506032b6570032100", "hex"),
                                                                      Buffer.from(Base.address_to_bytes(user)) ]),
                                                format : "der", type : "spki" }),
                                            Buffer.from(transaction.signatures[user])));
            }

            // An invalid signature is still caught
            mock = make_cluster();
            let bad_signer = SDK.create_callback_signer(user, async () => Base.base58_encode(random_bytes(64)));
            await check_rejects("without WebCrypto: an invalid signature is rejected", mock,
                                () => make_wallet(mock, user).thaw_entry(entry, bad_signer), "not " + user);
        }
        finally {
            fs.rmSync(key_dir, { recursive : true, force : true });
            for (let [ name, original ] of disabled) {
                subtle[name] = original;
            }
        }
    }

    results.finish();
}


main().then(() => process.exit(0)).catch((error) => {
    console.log("FAIL: " + results.name + ": " + error.stack);
    process.exit(1);
});
