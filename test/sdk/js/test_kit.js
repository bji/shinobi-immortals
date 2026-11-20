'use strict';

// Tests that the instructions of sdk/js/shinobi_immortals_base.js can be used directly with @solana/kit, as they are
// meant to be: that the SDK's AccountRole values are @solana/kit's, and that a transaction message built by
// @solana/kit from each kind of instruction, compiled, encoded, and decoded again, holds exactly that instruction.
// Both legacy and version 0 transactions are checked.
//
// This needs @solana/kit from npm (see package.json); it is skipped if that is not installed.

const { Base, Results, random_address, use_packages, sample_instructions, transaction_permissions,
        to_json } = require("./common.js");

const results = new Results("kit");

if (!use_packages(results.name)) {
    process.exit(0);
}

const Kit = require("@solana/kit");


// The SDK's roles are @solana/kit's
for (let name of [ "READONLY", "WRITABLE", "READONLY_SIGNER", "WRITABLE_SIGNER" ]) {
    results.check("AccountRole." + name + " is @solana/kit's", Base.AccountRole[name] === Kit.AccountRole[name],
                  "the SDK has " + Base.AccountRole[name] + ", @solana/kit has " + Kit.AccountRole[name]);
}
results.check("AccountRole has exactly @solana/kit's roles",
              to_json(Object.keys(Base.AccountRole).sort()) ==
              to_json(Object.keys(Kit.AccountRole).filter((k) => isNaN(Number(k))).sort()));


// Builds, compiles, and encodes with @solana/kit a transaction holding the instruction, then decodes it with
// @solana/kit and returns the instruction that it holds, in the same form as transaction_permissions gives
function round_trip(instruction, fee_payer, version)
{
    let message = Kit.pipe(
        Kit.createTransactionMessage({ version : version }),
        (m) => Kit.setTransactionMessageFeePayer(Kit.address(fee_payer), m),
        (m) => Kit.setTransactionMessageLifetimeUsingBlockhash({ blockhash : Kit.blockhash(random_address()),
                                                                 lastValidBlockHeight : 1000n }, m),
        (m) => Kit.appendTransactionMessageInstruction(instruction, m));

    let wire = Kit.getTransactionEncoder().encode(Kit.compileTransaction(message));

    let transaction = Kit.getTransactionDecoder().decode(wire);
    let compiled = Kit.getCompiledTransactionMessageDecoder().decode(transaction.messageBytes);
    let decompiled = Kit.decompileTransactionMessage(compiled);

    if (decompiled.instructions.length != 1) {
        throw new Error(decompiled.instructions.length + " instructions");
    }

    let decoded = decompiled.instructions[0];
    return {
        program : decoded.programAddress,
        accounts : decoded.accounts.map((a) => [ a.address, (a.role & 2) != 0, (a.role & 1) != 0 ]),
        data : Base.bytes_to_hex(decoded.data)
    };
}


let names = new Set();
for (let { name, signer, instruction } of sample_instructions()) {
    names.add(name.replace(/ \(.*/, ""));
    let expected = {
        program : instruction.programAddress,
        accounts : transaction_permissions(instruction, signer),
        data : Base.bytes_to_hex(instruction.data)
    };
    for (let version of [ "legacy", 0 ]) {
        let actual;
        try {
            actual = round_trip(instruction, signer, version);
        }
        catch (error) {
            results.check(name + " (" + version + ")", false, "@solana/kit failed: " + error.message);
            continue;
        }
        results.check(name + " (" + version + ")", to_json(actual) == to_json(expected),
                      "decoded " + to_json(actual).substring(0, 300) + ", expected " +
                      to_json(expected).substring(0, 300));
    }
}

// Every builder was used
let builders = Object.keys(Base).filter((k) => /^make_\w+_instruction$/.test(k));
results.check("every builder is used (" + builders.length + " builders, " + names.size + " instructions)",
              names.size == builders.length);


results.finish();
