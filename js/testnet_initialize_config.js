import * as web3 from '@solana/web3.js';
import bs58 from 'bs58';

(async () => {
    // Connect to cluster
    var connection = new web3.Connection(
        web3.clusterApiUrl('testnet'),
        'confirmed',
    );
    
    var from = web3.Keypair.fromSecretKey(Uint8Array.from([221,33,153,79,55,179,192,93,65,59,23,93,224,81,51,142,42,152,173,109,109,202,221,100,25,75,41,30,251,248,139,190,140,123,159,32,183,192,240,71,36,27,182,42,102,139,58,86,193,148,113,69,72,67,249,174,88,201,238,87,72,40,144,223]));

    var transaction = testnet_initialize_config({ });

    console.log("Transaction is " + JSON.stringify(transaction, null, "    "));

    // Sign transaction, broadcast, and confirm
    var signature = await web3.sendAndConfirmTransaction(
        connection,
        transaction,
        [from],
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
 * testnet_initialize_config
 *
 * Initializes the nifty stakes config program, transferring {funding_lamports} to the newly created account.
 *
 * Input args object contains these fields:
 * {
 * }
 *
 * Returns a String error or a Transaction object
**/
function testnet_initialize_config(args)
{
  let tmp;
  let keys;
  let data;

  let transaction = new web3.Transaction();


  keys = [ ];
  keys.push({ pubkey : new web3.PublicKey("ATPQjic4Jd8ra1hcXBNPp7MinWr9CmdcmFjSZrQ9ZZUa"), isSigner : true, isWritable : true });
  keys.push({ pubkey : new web3.PublicKey("9GqWv8fd6XyJ41wLLtVih82EgGfF6CEEvWgMfS6fGbTC"), isSigner : false, isWritable : true });
  keys.push({ pubkey : new web3.PublicKey("11111111111111111111111111111111"), isSigner : false, isWritable : false });


  data = [ ];

  solxact_push_64bit(1500000, false, data);

  transaction.add({ keys : keys, programId : new web3.PublicKey("EqJ4VfQE4P5bUrsD3YjVRBWad7JnyyzZGDZCMdUP15bG"), data : data });

  return transaction;
}
