import * as web3 from '@solana/web3.js';
import bs58 from 'bs58';

(async () => {

    // I DON'T HAVE TIME FOR THIS JAVASCRIPT FUCKING BULLSHIT.  CAN'T FIGURE OUT HOW TO IMPORT FUCKING FS SO I CAN
    // LOAD A FILE.
//    const myArgs = process.argv.slice(1);
//
//    let rawdata = fs.readFileSync(myArgs[0]);
//
//    let json = JSON.parse(rawdata);
//
//    console.log(json);

    // Connect to cluster
    var connection = new web3.Connection(
        web3.clusterApiUrl('testnet'),
        'confirmed',
    );
    
    var from = web3.Keypair.fromSecretKey(Uint8Array.from([221,33,153,79,55,179,192,93,65,59,23,93,224,81,51,142,42,152,173,109,109,202,221,100,25,75,41,30,251,248,139,190,140,123,159,32,183,192,240,71,36,27,182,42,102,139,58,86,193,148,113,69,72,67,249,174,88,201,238,87,72,40,144,223]));

    var to_create = web3.Keypair.fromSecretKey(Uint8Array.from([184,162,65,31,209,193,197,114,127,161,34,177,215,27,177,75,86,254,192,76,49,123,107,44,246,116,43,189,104,163,149,118,137,219,151,32,231,111,245,133,252,52,146,181,66,145,219,64,149,125,67,21,129,31,71,250,205,170,183,33,85,43,122,73]));

    var transaction = create_account({ funding_account : from.publicKey,
                                       lamports : 1000000,
                                       new_account : to_create.publicKey,
                                       owner : from.publicKey + "",
                                       space : 64
                                     });

    console.log("Transaction is " + JSON.stringify(transaction, null, "    "));

    // Sign transaction, broadcast, and confirm
    var signature = await web3.sendAndConfirmTransaction(
        connection,
        transaction,
        [from, to_create],
    );
    console.log('SIGNATURE', signature);
    
})();


function solxact_push_bool(value, into)
{
    into.push(value ? 1 : 0);
}

function solxact_push_16bit(value, big_endian, into)
{
    let str = value.toString().toLowerCase();

    if (str.startsWith("0x")) {
        value = parseInt(str, 16);
    }
    else {
        value = parseInt(str, 10);
    }

    if (big_endian) {
        into.push((value >>> 8) & 0xFF);
        into.push((value >>> 0) & 0xFF);
    }
    else {
        into.push((value >>> 0) & 0xFF);
        into.push((value >>> 8) & 0xFF);
    }
}


function solxact_push_32bit(value, big_endian, into)
{
    let str = value.toString().toLowerCase();

    if (str.startsWith("0x")) {
        value = parseInt(str, 16);
    }
    else {
        value = parseInt(str, 10);
    }

    if (big_endian) {
        into.push((value >>> 24) & 0xFF);
        into.push((value >>> 16) & 0xFF);
        into.push((value >>>  8) & 0xFF);
        into.push((value >>>  0) & 0xFF);
    }
    else {
        into.push((value >>>  0) & 0xFF);
        into.push((value >>>  8) & 0xFF);
        into.push((value >>> 16) & 0xFF);
        into.push((value >>> 24) & 0xFF);
    }
}


function solxact_push_64bit(value, big_endian, into)
{
    const ascii_0 = '0'.charCodeAt(0);
    const ascii_9 = '9'.charCodeAt(0);
    const ascii_a = 'a'.charCodeAt(0);
    const ascii_minus = '-'.charCodeAt(0);
    
    let str = value.toString().toLowerCase();

    let array = [ ];

    if (str.startsWith("0x")) {
        str = str.substring(2);
        let i = str.length;
        while (i > 0) {
            let n = 0;
            let c = str.charCodeAt(--i);
            if ((c >= ascii_0) && (c <= ascii_9)) {
                n = (c - ascii_0);
            }
            else {
                n = ((c - ascii_a) + 10);
            }
            if (i > 0) {
                n <<= 4;
                c = str.charCodeAt(--i);
                if ((c >= ascii_0) && (c <= ascii_9)) {
                    n += (c - ascii_0);
                }
                else {
                    n += ((c - ascii_a) + 10);
                }
            }
            array.unshift(n & 0xFF);
        }
        while (array.length < 8) {
            array.unshift(0);
        }
    }
    else {
        let negative = false;
        if (str.charCodeAt(0) == ascii_minus) {
            negative = true;
            str = str.substring(1);
        }
        // Convert to 16 hex digits first
        for (let i = 0; i < 16; i++) {
            array.push(0);
        }
        for (let i = 0; i < str.length; i++) {
            for (let j = 0; j < 16; j++) {
                array[j] *= 10;
            }
            let k = 15;
            array[k] += str.charCodeAt(i) - ascii_0;
            while (k >= 0) {
                let j = k;
                while (array[j] > 15) {
                    do {
                        array[j] -= 16;
                        array[j - 1] += 1;
                    } while (array[j] > 15);
                    j--;
                }
                k--;
            }
        }
        // Subract 1 and invert if negative
        if (negative) {
            let k = 15;
            array[k] -= 1;
            while (k >= 0) {
                if (array[k] == -1) {
                    array[k] = 15;
                    array[k - 1] -= 1;
                }
                array[k] = ~array[k] & 0xF;
                k--;
            }
            array[0] |= 8;
        }
        // Convert hex to base-256
        let hex_array = [ ];
        for (let i = 0; i < 16; i += 2) {
            hex_array.push((array[i] * 16) + array[i + 1]);
        }
        array = hex_array;
    }

    if (big_endian) {
        for (let i = 0; i < array.length; i++) {
            into.push(array.at(i));
        }
    }
    else {
        for (let i = 0; i < array.length; i++) {
            into.push(array.at(-(i + 1)));
        }
    }
}


function solxact_push_bincodevarint_unsigned(value, big_endian, into)
{
    // Convert to 64 bit big-endian
    let array = [ ];
    solxact_push_64bit(value, true, array);

    solxact_push_bincode(array, big_endian, into);
}


function solxact_push_bincodevarint_signed(value, big_endian, into)
{
    let str = value.toString();
    let negative = false;
    if (str.charCodeAt(0) == '-'.charCodeAt(0)) {
        // Absolute value
        negative = true;
        str = str.substring(1);
    }
    
    // Convert to 64 bit big-endian
    let array = [ ];
    solxact_push_64bit(str, true, array);

    // Double
    for (let i = 0; i < 8; i++) {
        array[i] *= 2;
    }

    // If negative, subtract 1
    if (negative) {
        array[7] -= 1;
    }

    // Normalize to base 256
    let k = 8;
    while (k >= 0) {
        if (array[k] == -1) {
            array[k] = 255;
            array[k - 1] -= 1;
        }
        if (array[k] > 255) {
            array[k] -= 256;
            array[k - 1] += 1;
        }
        k--;
    }

    solxact_push_bincode(array, big_endian, into);
}


function solxact_push_bincode(array, big_endian, into)
{
    for (let i = 0; i < 4; i++) {
        if (array[i] != 0) {
            into.push(253);
            if (big_endian) {
                for (let k = 0; k < 8; k++) {
                    into.push(array[k]);
                }
            }
            else {
                for (let k = 7; k >= 0; k--) {
                    into.push(array[k]);
                }
            }
            return;
        }
    }

    for (let i = 4; i < 6; i++) {
        if (array[i] != 0) {
            into.push(252);
            if (big_endian) {
                for (let k = 4; k < 8; k++) {
                    into.push(array[k]);
                }
            }
            else {
                for (let k = 7; k >= 4; k--) {
                    into.push(array[k]);
                }
            }
            return;
        }
    }

    if ((array[6] != 0) || (array[7] > 250)) {
        into.push(251);
        if (big_endian) {
            for (let k = 6; k < 8; k++) {
                into.push(array[k]);
            }
        }
        else {
            for (let k = 7; k >= 6; k--) {
                into.push(array[k]);
            }
        }
        return;
    }

    into.push(array[7]);
}


function solxact_push_pubkey(string, into)
{
    let decoded = bs58.decode(string);

    if (decoded.length != 32) {
        return "Invalid pubkey: " + string;
    }

    for (let i = 0; i < decoded.length; i++) {
        into.push(decoded.at(i));
    }
}


function solxact_push_string_zero_terminated(string, into)
{
    solxact_push_string_bytes(string, into);
    into.push(0);
}


function solxact_push_string_length_encoded(string, length_size, big_endian, into)
{
    let array = [ ];
    solxact_push_string_bytes(string, array);

    if (length_size == 1) {
        into.push(array.length & 0xFF);
    }
    else {
        solxact_push_16bit(array.length, big_endian, into);
    }

    for (let i = 0; i < array.length; i++) {
        into.push(array[i]);
    }
}


function solxact_push_string_bytes(string, into)
{
    // Javascript sucks.  Must manually encode to UTF-8 byte array.
    for (let i = 0; i < string.length; i++) {
        var charcode = string.charCodeAt(i);
        if (charcode < 0x80) {
            into.push(charcode);
        }
        else if (charcode < 0x800) {
            into.push(0xc0 | (charcode >> 6), 
                      0x80 | (charcode & 0x3f));
        }
        else if ((charcode < 0xd800) || (charcode >= 0xe000)) {
            into.push(0xe0 | (charcode >> 12), 
                      0x80 | ((charcode >> 6) & 0x3f), 
                      0x80 | (charcode & 0x3f));
        }
        // surrogate pair
        else {
            i++;
            // UTF-16 encodes 0x10000 - 0x10FFFF by subtracting 0x10000 and splitting the 20 bits
            // of 0x0 - 0xFFFFF into two halves
            charcode = (0x10000 + (((charcode & 0x3ff) << 10) | (string.charCodeAt(i) & 0x3ff)));
            into.push(0xf0 | (charcode >> 18), 
                      0x80 | ((charcode >> 12) & 0x3f), 
                      0x80 | ((charcode >> 6) & 0x3f), 
                      0x80 | (charcode & 0x3f));
        }
    }
}


function solxact_push_array_bytes(array, into)
{
    for (let i = 0; i < array.length; i++) {
        into.push(array[i]);
    }
}


/**
 * create_account
 *
 * Invokes the system program to create a new system account with address {new_account} of size {space} bytes.  This new account will be owned by the program account {owner}.  {lamports} lamports will be transferred to this new account from {funding_account}.
 *
 * Input args object contains these fields:
 * {
 *     // This account provides the lamports which are transferred into the newly created account
 *     funding_account : pubkey string,
 *     // This is the number of lamports to transfer into the newly created account
 *     lamports : unsigned integer (min 0, max 18446744073709551615) (use string for values > 53 bits),
 *     // This is the address of the account which will be created
 *     new_account : pubkey string,
 *     // This is the address of the program that will own the newly created account
 *     owner : string (min length 0, max length 65535,
 *     // This is the number of bytes to allocate for the account
 *     space : unsigned integer (min 0, max 18446744073709551615) (use string for values > 53 bits)
 * }
 *
 * Returns a String error or a Transaction object
**/
function create_account(args)
{
  let tmp;
  let keys;
  let data;

  let transaction = new web3.Transaction();


  keys = [ ];
  keys.push({ pubkey : new web3.PublicKey(args.funding_account), isSigner : true, isWritable : true });
  keys.push({ pubkey : new web3.PublicKey(args.new_account), isSigner : true, isWritable : true });


  data = [ ];

  solxact_push_32bit(0, false, data);

  solxact_push_64bit(args.lamports, false, data);

  solxact_push_64bit(args.space, false, data);

  solxact_push_pubkey(args.owner, data);

  transaction.add({ keys : keys, programId : new web3.PublicKey("11111111111111111111111111111111"), data : data });

  return transaction;
}
