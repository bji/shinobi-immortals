#!/bin/sh

function usage_exit ()
{
    echo "Usage: show.sh [-u $RPC_SPECIFIER] config"
    echo "       show.sh [-u $RPC_SPECIFIER] block <GROUP_NUMBER> <BLOCK_NUMBER>"
    echo "       show.sh [-u $RPC_SPECIFIER] entry <GROUP_NUMBER> <BLOCK_NUMBER> <ENTRY_INDEX>"
    echo
    echo "RPC_SPECIFIER, if provided, is either a URL, or a mnemonic:"
    echo "l, localhost : http://localhost:8899"
    echo "d, devnet : https://api.devnet.solana.com"
    echo "t, testnet : https://api.testnet.solana.com"
    echo "m, mainnet : https://api.mainnet-beta.solana.com"
    echo
    echo "If RPC_SPECIFIER is not provided, the default is \"mainnet\"."
    exit 1
}


function pda ()
{
    solpda $@ | cut -d . -f 1
}

function normalize_float ()
{
    local float=$1

    while [ "${float%%0}" != "$float" ]; do
        float="${float%%0}"
    done

    if [ "${float%%.}" != "$float" ]; then
        float="${float%%.}"
    fi

    if [ -z "$float" ]; then
        echo "0"
    elif [ "${float:0:1}" = "." ]; then
        echo "0$float"
    else
        echo $float
    fi
}

function to_sol ()
{
    local lamports=$1

    normalize_float `echo "20 k $lamports 1000 1000 1000 * * / p" | dc`
}

function to_bool ()
{
    local value=$1

    if [ "0$value" -ne 0 ]; then
        echo true
    else
        echo false
    fi
}

function to_duration ()
{
    local T=${1%.*}
    local F=$(echo "$1 $T - p" | dc)
    local D=$((T/60/60/24))
    local H=$((T/60/60%24))
    local M=$((T/60%60))
    local S=$((T%60))
    
    (($D > 0)) && printf '%d day%s ' $D $((($D > 1)) && echo s)
    (($D+$H > 0)) && printf '%d hr%s ' $H $((($H > 1)) && echo s)
    (($D+$H+$M > 0)) && printf '%d min%s ' $M $((($M > 1)) && echo s)

    S=$(echo "$S $F + p" | dc)
    printf '%0.2f sec' $S
}

function to_timestamp ()
{
    local now=`date -u +%s`

    local timestamp=$1

    echo -n `date -d @$timestamp` "("

    if [ "0$timestamp" -gt "0$now" ]; then
        echo -n "in "
        echo -n `to_duration $(($timestamp-$now))`
    else
        echo -n `to_duration $(($now-$timestamp))`
        echo -n " ago"
    fi

    echo -n ")"
}

function to_commission ()
{
    local commission=$1

    normalize_float `echo "20 k $commission 65536 / p" | dc`
}

function bit_bool ()
{
    local value=$1
    local bit=$2

    to_bool $(($value & $((1 << $bit))))
}

# Data is piped in
function to_base58 ()
{
    local -a base58_chars=(
        1 2 3 4 5 6 7 8 9
        A B C D E F G H   J K L M N   P Q R S T U V W X Y Z
        a b c d e f g h i j k   m n o p q r s t u v w x y z
    )
    cat | xxd -p -u | tr -d '\n' |
        {
            read hex
            while [[ "$hex" =~ ^00 ]]; do
                echo -n 1; hex="${hex:2}"
            done
            if test -n "$hex"; then
                dc -e "16i0$hex Ai[58~rd0<x]dsxx+f" |
                    while read -r
                    do echo -n "${base58_chars[REPLY]}"
                    done
            fi
            echo
        }
}

function get_account_data ()
{
    local RPC_URL=$1
    local ACCOUNT_PUBKEY=$2
    local DATA_OFFSET=$3
    local DATA_LEN=$4

    if [ -n "$DATA_OFFSET" ]; then
        DATA_SLICE="\"dataSlice\":{\"offset\":$DATA_OFFSET,\"length\":$DATA_LEN},"
    else
        DATA_SLICE=
    fi

    DATA=`curl -s $RPC_URL -X POST -H "Content-Type: application/json" -d "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"getAccountInfo\",\"params\":[\"$ACCOUNT_PUBKEY\",{$DATA_SLICE\"encoding\":\"base64\"}]}" | jq -r ".result.value.data[0]"`

    if [ "$DATA" = "null" ]; then
        echo -n ""
    else
        echo -n "$DATA"
    fi
}


# Given base64 data $2 (as loaded by get_account_data), returns the numeric u8 value at offset $1
function get_data_u8 ()
{
    local offset=$1
    
    echo "$2" | base64 -d | od -An -j$offset -N1 -tu1 | tr -d [:space:]
}


# Given base64 data $2 (as loaded by get_account_data), returns the numeric u16 value at offset $1
function get_data_u16 ()
{
    local offset=$1
    
    echo "$2" | base64 -d | od -An -j$offset -N2 -tu2 | tr -d [:space:]
}


# Given base64 data $2 (as loaded by get_account_data), returns the numeric u32 value at offset $1
function get_data_u32 ()
{
    local offset=$1
    
    echo "$2" | base64 -d | od -An -j$offset -N4 -tu4 | tr -d [:space:]
}


# Given base64 data $2 (as loaded by get_account_data), returns the numeric u64 value at offset $1
function get_data_u64 ()
{
    local offset=$1
    
    echo "$2" | base64 -d | od -An -j$offset -N8 -tu8 | tr -d [:space:]
}

# Given base64 data $2 (as loaded by get_account_data), returns the pubkey value at offset $1 (as Base58 string)
function get_data_pubkey ()
{
    local offset=$1
    echo "$2" | base64 -d | dd bs=1 count=32 skip=4 status=none | to_base58
}


RPC_URL=https://api.mainnet-beta.solana.com

if [ "$1" = "-u" ]; then
    shift
    if [ -z "$1" ]; then
        usage_exit
    fi
    case $1 in
        l | localhost) RPC_URL=http://localhost:8899 ;;
        d | devnet) RPC_URL=https://api.devnet.solana.com ;;
        t | testnet) RPC_URL=https://api.testnet.solana.com ;;
        m | mainnet) ;;
        *) RPC_URL=$1 ;;
    esac
    shift
fi

if [ -z "$PROGRAM_PUBKEY" ]; then
    PROGRAM_PUBKEY=Shin1cdrR1pmemXZU3yDV3PnQ48SX9UmrtHF4GbKzWG
fi

# Compute pubkeys

      SYSTEM_PROGRAM_PUBKEY=$(echo 11111111111111111111111111111111)
   SPL_TOKEN_PROGRAM_PUBKEY=$(echo TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA)
      SPLATA_PROGRAM_PUBKEY=$(echo ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL)
       STAKE_PROGRAM_PUBKEY=$(echo Stake11111111111111111111111111111111111111)
        CLOCK_SYSVAR_PUBKEY=$(echo SysvarC1ock11111111111111111111111111111111)
         RENT_SYSVAR_PUBKEY=$(echo SysvarRent111111111111111111111111111111111)
    METAPLEX_PROGRAM_PUBKEY=$(echo metaqbxxUerdq28cj1RbAWkYQm3ybzjb6a8bt518x1s)
STAKE_HISTORY_SYSVAR_PUBKEY=$(echo SysvarStakeHistory1111111111111111111111111)
        STAKE_CONFIG_PUBKEY=$(echo StakeConfig11111111111111111111111111111111)
              CONFIG_PUBKEY=$(pda $PROGRAM_PUBKEY u8[1])
           AUTHORITY_PUBKEY=$(pda $PROGRAM_PUBKEY u8[2])
        MASTER_STAKE_PUBKEY=$(pda $PROGRAM_PUBKEY u8[3])
             KI_MINT_PUBKEY=$(pda $PROGRAM_PUBKEY u8[4])
     BID_MARKER_MINT_PUBKEY=$(pda $PROGRAM_PUBKEY u8[11])
         KI_METADATA_PUBKEY=$(pda $METAPLEX_PROGRAM_PUBKEY                                                            \
                                  String[metadata]                                                                    \
                                  Pubkey[$METAPLEX_PROGRAM_PUBKEY]                                                    \
                                  Pubkey[$KI_MINT_PUBKEY])
 BID_MARKER_METADATA_PUBKEY=$(pda $METAPLEX_PROGRAM_PUBKEY                                                            \
                                  String[metadata]                                                                    \
                                  Pubkey[$METAPLEX_PROGRAM_PUBKEY]                                                    \
                                  Pubkey[$BID_MARKER_MINT_PUBKEY])

case $1 in

    config)

        shift

        if [ -n "$1" ]; then
            usage_exit
        fi

        ACCOUNT_DATA=`get_account_data $RPC_URL $CONFIG_PUBKEY`

        if [ -z "$ACCOUNT_DATA" ]; then
            echo "Config account does not exist"
            exit 0
        fi

        DATA_TYPE=`get_data_u32 0 "$ACCOUNT_DATA"`

        if [ "0$DATA_TYPE" -ne 1 ]; then
            echo "Invalid data type: $DATA_TYPE"
            exit 1
        fi

        ADMIN_PUBKEY=`get_data_pubkey 4 "$ACCOUNT_DATA"`

        echo '{"config_account":"$CONFIG_PUBKEY","admin_pubkey":"$ADMIN_PUBKEY"}'
    ;;

    block)

        shift
        
        if [ -z "$2" ]; then
            usage_exit
        fi

        BLOCK_PUBKEY=`pda $PROGRAM_PUBKEY u8[14] u32[$1] u32[$2]`

        ACCOUNT_DATA=`get_account_data $RPC_URL $BLOCK_PUBKEY`

        if [ -z "$ACCOUNT_DATA" ]; then
            echo "Block account $1 $2 does not exist"
            exit 1
        fi

        ACCOUNT_DATA_LEN=`echo "$ACCOUNT_DATA" | base64 -d | wc -c`

        if [ $ACCOUNT_DATA_LEN -ne 122 ]; then
            echo "Block account has invalid size $ACCOUNT_DATA_LEN, expected 122"
            exit 1
        fi

        DATA_TYPE=`get_data_u32 0 "$ACCOUNT_DATA"`

        if [ "0$DATA_TYPE" -ne 2 ]; then
            echo "Invalid data type: $DATA_TYPE"
            exit 1
        fi

        echo -n '{'

        echo -n '"block_account":"'$BLOCK_PUBKEY'",'

        echo -n '"configuration":{'
        
        echo -n '"group_number":'`get_data_u32 8 "$ACCOUNT_DATA"`','

        echo -n '"block_number":'`get_data_u32 12 "$ACCOUNT_DATA"`','

        TOTAL_ENTRY_COUNT=`get_data_u16 16 "$ACCOUNT_DATA"`
        
        echo -n '"total_entry_count":'$TOTAL_ENTRY_COUNT','

        echo -n '"total_mystery_count":'`get_data_u16 18 "$ACCOUNT_DATA"`','

        echo -n '"mystery_start_price":'`to_sol \`get_data_u64 24 "$ACCOUNT_DATA"\``','
        
        DURATION=`get_data_u32 32 "$ACCOUNT_DATA"`
        
        echo -n '"reveal_period_duration":'$DURATION','
        
        echo -n '"reveal_period_duration_display":"'`to_duration $DURATION`'",'
        
        echo -n '"minimum_price":'`to_sol \`get_data_u64 40 "$ACCOUNT_DATA"\``','

        echo -n '"has_auction":'`to_bool \`get_data_u8 48 "$ACCOUNT_DATA"\``','

        DURATION=`get_data_u32 52 "$ACCOUNT_DATA"`

        echo -n '"duration":'$DURATION','

        echo -n '"duration_display":"'`to_duration $DURATION`'",'

        echo -n '"final_start_price":'`to_sol \`get_data_u64 56 "$ACCOUNT_DATA"\``','

        DURATION=`get_data_u32 64 "$ACCOUNT_DATA"`
        
        echo -n '"whitelist_duration":'$DURATION','
        
        echo -n '"whitelist_duration_display":"'`to_duration $DURATION`'"'

        echo -n '},'

        echo -n '"added_entries_count":'`get_data_u16 68 "$ACCOUNT_DATA"`','

        TIMESTAMP=`get_data_u64 72 "$ACCOUNT_DATA"`

        echo -n '"block_start_timestamp":'$TIMESTAMP','

        if [ "0$TIMESTAMP" -ne 0 ]; then
            echo -n '"block_start_timestamp_display":"'`to_timestamp $TIMESTAMP`'",'
        fi
        
        echo -n '"mysteries_sold_count":'`get_data_u16 80 "$ACCOUNT_DATA"`','

        TIMESTAMP=`get_data_u64 88 "$ACCOUNT_DATA"`

        echo -n '"mystery_phase_end_timestamp":'$TIMESTAMP','

        if [ "0$TIMESTAMP" -ne 0 ]; then
            echo -n '"mystery_phase_end_timestamp_display":"'`to_timestamp $TIMESTAMP`'",'
        fi

        echo -n '"commission":'`to_commission \`get_data_u16 96 "$ACCOUNT_DATA"\``','

        echo -n '"last_commission_change_epoch":'`get_data_u64 104 "$ACCOUNT_DATA"`','

        echo -n '"entries_added":['

        # Skip to the entries added bitmap
        BITMAP_BYTES=`echo "$ACCOUNT_DATA" | base64 -d | dd bs=1 skip=112 status=none | od -An -tu8`

        N=0
        COMMA=
        for byte in $BITMAP_BYTES; do
            [ $N -eq "0$TOTAL_ENTRY_COUNT" ] && break;
            [ -n "$COMMA" ] && echo -n ,;
            COMMA=1
            echo -n `bit_bool $byte 0`
            [ $((N+1)) -eq "0$TOTAL_ENTRY_COUNT" ] && break;
            [ -n "$COMMA" ] && echo -n ,;
            echo -n `bit_bool $byte 1`
            [ $((N+2)) -eq "0$TOTAL_ENTRY_COUNT" ] && break;
            [ -n "$COMMA" ] && echo -n ,;
            echo -n `bit_bool $byte 2`
            [ $((N+3)) -eq "0$TOTAL_ENTRY_COUNT" ] && break;
            [ -n "$COMMA" ] && echo -n ,;
            echo -n `bit_bool $byte 3`
            [ $((N+4)) -eq "0$TOTAL_ENTRY_COUNT" ] && break;
            [ -n "$COMMA" ] && echo -n ,;
            echo -n `bit_bool $byte 4`
            [ $((N+5)) -eq "0$TOTAL_ENTRY_COUNT" ] && break;
            [ -n "$COMMA" ] && echo -n ,;
            echo -n `bit_bool $byte 5`
            [ $((N+6)) -eq "0$TOTAL_ENTRY_COUNT" ] && break;
            [ -n "$COMMA" ] && echo -n ,;
            echo -n `bit_bool $byte 6`
            [ $((N+7)) -eq "0$TOTAL_ENTRY_COUNT" ] && break;
            [ -n "$COMMA" ] && echo -n ,;
            echo -n `bit_bool $byte 7`
            N=$(($N+8))
        done

        echo ']}'
    ;;

    entry)

    ;;

    *)
        usage_exit
        ;;
esac
