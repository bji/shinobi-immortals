#!/bin/sh

function usage_exit ()
{
    echo "Usage: show.sh [-u $RPC_SPECIFIER] config"
    echo "       show.sh [-u $RPC_SPECIFIER] block <GROUP_NUMBER> <BLOCK_NUMBER>"
    echo "       show.sh [-u $RPC_SPECIFIER] whitelist <GROUP_NUMBER> <BLOCK_NUMBER>"
    echo "       show.sh [-u $RPC_SPECIFIER] entry <GROUP_NUMBER> <BLOCK_NUMBER> <ENTRY_INDEX>"
    echo "       show.sh [-u $RPC_SPECIFIER] metaplex <GROUP_NUMBER> <BLOCK_NUMBER> <ENTRY_INDEX>"
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
    xxd -p -u | tr -d '\n' |
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

function to_hex ()
{
    if [ -z "$1" ]; then
        od -An -tx1 -v | tr -d '[:space:]'
    else
        printf "%032x" $1
    fi
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
    
    echo "$2" | base64 -d | od -An -j$offset -N1 -tu1 -v | tr -d [:space:]
}

# Given base64 data $2 (as loaded by get_account_data), returns the numeric u16 value at offset $1
function get_data_u16 ()
{
    local offset=$1
    
    echo "$2" | base64 -d | od -An -j$offset -N2 -tu2 -v | tr -d [:space:]
}

# Given base64 data $2 (as loaded by get_account_data), returns the numeric u32 value at offset $1
function get_data_u32 ()
{
    local offset=$1
    
    echo "$2" | base64 -d | od -An -j$offset -N4 -tu4 -v | tr -d [:space:]
}

# Given base64 data $2 (as loaded by get_account_data), returns the numeric u64 value at offset $1
function get_data_u64 ()
{
    local offset=$1
    
    echo "$2" | base64 -d | od -An -j$offset -N8 -tu8 -v | tr -d [:space:]
}

# Given base64 data $2 (as loaded by get_account_data), returns the bool value at offset $1
function get_data_bool ()
{
    local bv=`get_data_u8 $1 "$2"`

    if [ "0$bv" -eq 0 ]; then
        echo false
    else
        echo true
    fi
}

# Given base64 data $2 (as loaded by get_account_data), returns the pubkey value at offset $1 (as Base58 string)
function get_data_pubkey ()
{
    local offset=$1
    echo "$2" | base64 -d | dd bs=1 count=32 skip=$offset status=none | to_base58
}

function get_data_sha256 ()
{
    local offset=$1
    echo "$2" | base64 -d | dd bs=1 count=32 skip=$offset status=none | to_hex
}

function get_data_string ()
{
    local offset=$1
    local max_length=$2
    echo "$3" | base64 -d | dd bs=1 count=$max_length skip=$offset status=none | tr -d '\0'
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
        
        if [ -z "$1" -o -z "$2" -o -n "$3" ]; then
            usage_exit
        fi

        BLOCK_PUBKEY=`pda $PROGRAM_PUBKEY u8[14] u32[$1] u32[$2]`

        ACCOUNT_DATA=`get_account_data $RPC_URL $BLOCK_PUBKEY`

        if [ -z "$ACCOUNT_DATA" ]; then
            echo "Block account $1 $2 does not exist"
            exit 1
        fi

        ACCOUNT_DATA_LEN=`echo "$ACCOUNT_DATA" | base64 -d | wc -c`

        if [ $ACCOUNT_DATA_LEN -lt 120 ]; then
            echo "Block account has invalid size $ACCOUNT_DATA_LEN, expected at least 120"
            exit 1
        fi

        DATA_TYPE=`get_data_u32 0 "$ACCOUNT_DATA"`

        if [ "0$DATA_TYPE" -ne 2 ]; then
            echo "Invalid data type: $DATA_TYPE"
            exit 1
        fi

        echo -n '{'

        echo -n '"block_pubkey":"'$BLOCK_PUBKEY'",'

        echo -n '"configuration":{'
        
        echo -n '"group_number":'`get_data_u32 8 "$ACCOUNT_DATA"`','

        echo -n '"block_number":'`get_data_u32 12 "$ACCOUNT_DATA"`','

        TOTAL_ENTRY_COUNT=`get_data_u16 16 "$ACCOUNT_DATA"`
        
        echo -n '"total_entry_count":'$TOTAL_ENTRY_COUNT','

        MYSTERY_COUNT=`get_data_u16 18 "$ACCOUNT_DATA"`

        echo -n '"total_mystery_count":'$MYSTERY_COUNT','

        if [ "0$MYSTERY_COUNT" -ne 0 ]; then
            echo -n '"mystery_phase_duration":'`get_data_u32 20 "$ACCOUNT_DATA"`','

            echo -n '"mystery_start_price":'`to_sol \`get_data_u64 24 "$ACCOUNT_DATA"\``','
        fi
        
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

        echo -n '"added_entries_count":'`get_data_u16 72 "$ACCOUNT_DATA"`','

        TIMESTAMP=`get_data_u64 80 "$ACCOUNT_DATA"`

        echo -n '"block_start_timestamp":'$TIMESTAMP','

        if [ "0$TIMESTAMP" -ne 0 ]; then
            echo -n '"block_start_timestamp_display":"'`to_timestamp $TIMESTAMP`'",'
        fi

        if [ "0$MYSTERY_COUNT" -ne 0 ]; then
        
            echo -n '"mysteries_sold_count":'`get_data_u16 88 "$ACCOUNT_DATA"`','

            TIMESTAMP=`get_data_u64 96 "$ACCOUNT_DATA"`

            echo -n '"mystery_phase_end_timestamp":'$TIMESTAMP','

            if [ "0$TIMESTAMP" -ne 0 ]; then
                echo -n '"mystery_phase_end_timestamp_display":"'`to_timestamp $TIMESTAMP`'",'
            fi
        fi

        echo -n '"commission":'`to_commission \`get_data_u16 104 "$ACCOUNT_DATA"\``','

        echo -n '"last_commission_change_epoch":'`get_data_u64 112 "$ACCOUNT_DATA"`','

        echo -n '"entries_added":['

        # Skip to the entries added bitmap
        BITMAP_BYTES=`echo "$ACCOUNT_DATA" | base64 -d | dd bs=1 skip=120 status=none | od -An -tu1 -v`

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

    whitelist)

        shift
        
        if [ -z "$1" -o -z "$2" -o -n "$3" ]; then
            usage_exit
        fi

        BLOCK_PUBKEY=`pda $PROGRAM_PUBKEY u8[14] u32[$1] u32[$2]`

        WHITELIST_PUBKEY=`pda $PROGRAM_PUBKEY u8[13] Pubkey[$BLOCK_PUBKEY]`
        
        ACCOUNT_DATA=`get_account_data $RPC_URL $WHITELIST_PUBKEY`

        if [ -z "$ACCOUNT_DATA" ]; then
            echo "Whitelist account for $1 $2 does not exist"
            exit 1
        fi

        ACCOUNT_DATA_LEN=`echo "$ACCOUNT_DATA" | base64 -d | wc -c`

        if [ $ACCOUNT_DATA_LEN -ne 9608 ]; then
            echo "Whitelist account has invalid size $ACCOUNT_DATA_LEN, expected 9608"
            exit 1
        fi

        DATA_TYPE=`get_data_u32 0 "$ACCOUNT_DATA"`

        if [ "0$DATA_TYPE" -ne 5 ]; then
            echo "Invalid data type: $DATA_TYPE"
            exit 1
        fi

        LIST_COUNT=`get_data_u16 4 "$ACCOUNT_DATA"`

        if [ "0$LIST_COUNT" -eq 0 -o "0$LIST_COUNT" -gt 300 ]; then
            echo "Invalid list count: $LIST_COUNT"
            exit 1
        fi

        echo -n '{'

        echo -n '"block_pubkey":"'$BLOCK_PUBKEY'",'
        
        echo -n '"whitelist_pubkey":"'$WHITELIST_PUBKEY'",'

        echo -n '"whitelisted_pubkeys":['

        for i in `seq 1 $LIST_COUNT`; do
            if [ $i -gt 1 ]; then
                echo -n ","
            fi
            echo -n '"'`get_data_pubkey $((32*$i-26)) "$ACCOUNT_DATA"`'"'
        done

        echo -n ']}'
    ;;

    entry)

        shift
        
        if [ -z "$1" -o -z "$2" -o -z "$3" -o -n "$4" ]; then
            usage_exit
        fi

        BLOCK_PUBKEY=`pda $PROGRAM_PUBKEY u8[14] u32[$1] u32[$2]`

        MINT_PUBKEY=`pda $PROGRAM_PUBKEY u8[5] Pubkey[$BLOCK_PUBKEY] u16[$3]`

        ENTRY_PUBKEY=`pda $PROGRAM_PUBKEY u8[15] Pubkey[$MINT_PUBKEY]`

        ACCOUNT_DATA=`get_account_data $RPC_URL $ENTRY_PUBKEY`

        if [ -z "$ACCOUNT_DATA" ]; then
            echo "Entry account $1 $2 $3 does not exist"
            exit 1
        fi

        ACCOUNT_DATA_LEN=`echo "$ACCOUNT_DATA" | base64 -d | wc -c`

        if [ $ACCOUNT_DATA_LEN -ne 3032 ]; then
            echo "Block account has invalid size $ACCOUNT_DATA_LEN, expected 3032"
            exit 1
        fi

        DATA_TYPE=`get_data_u32 0 "$ACCOUNT_DATA"`

        if [ "0$DATA_TYPE" -ne 3 ]; then
            echo "Invalid data type: $DATA_TYPE"
            exit 1
        fi

        echo -n '{"entry_pubkey":"'$ENTRY_PUBKEY'",'

        echo -n '"block_pubkey":"'`get_data_pubkey 4 "$ACCOUNT_DATA"`'",'

        echo -n '"group_number":'`get_data_u32 36 "$ACCOUNT_DATA"`','
        
        echo -n '"block_number":'`get_data_u32 40 "$ACCOUNT_DATA"`','

        echo -n '"entry_index":'`get_data_u16 44 "$ACCOUNT_DATA"`','

        echo -n '"mint_pubkey":"'`get_data_pubkey 46 "$ACCOUNT_DATA"`'",'

        echo -n '"token_pubkey":"'`get_data_pubkey 78 "$ACCOUNT_DATA"`'",'

        echo -n '"metaplex_metadata_pubkey":"'`get_data_pubkey 110 "$ACCOUNT_DATA"`'",'

        echo -n '"minimum_price":'`to_sol \`get_data_u64 144 "$ACCOUNT_DATA"\``','

        echo -n '"has_auction":'`to_bool \`get_data_u8 152 "$ACCOUNT_DATA"\``','

        echo -n '"duration":'`get_data_u32 156 "$ACCOUNT_DATA"`','

        echo -n '"non_auction_start_price":'`to_sol \`get_data_u64 160 "$ACCOUNT_DATA"\``','

        echo -n '"reveal_sha256":"'`get_data_sha256 168 "$ACCOUNT_DATA"`'",'

        TIMESTAMP=`get_data_u64 200 "$ACCOUNT_DATA"`

        echo -n '"reveal_timestamp":'$TIMESTAMP','

        if [ "0$TIMESTAMP" -ne 0 ]; then
            echo -n '"reveal_timestamp_display":"'`to_timestamp $TIMESTAMP`'",'
        fi

        echo -n '"purchase_price":'`to_sol \`get_data_u64 208 "$ACCOUNT_DATA"\``','

        echo -n '"refund_awarded":'`to_bool \`get_data_u8 216 "$ACCOUNT_DATA"\``','

        echo -n '"commission":'`get_data_u16 218 "$ACCOUNT_DATA"`','

        echo -n '"auction":{'
        
        echo -n '"highest_bid":'`to_sol \`get_data_u64 224 "$ACCOUNT_DATA"\``

        PUBKEY=`get_data_pubkey 232 "$ACCOUNT_DATA"`

        if [ "$PUBKEY" != "11111111111111111111111111111111" ]; then
            echo -n ',"winning_bid_pubkey":'`get_data_pubkey 232 "$ACCOUNT_DATA"`
        fi

        echo -n '},"owned":{'

        PUBKEY=`get_data_pubkey 264 "$ACCOUNT_DATA"`

        if [ "$PUBKEY" != "11111111111111111111111111111111" ]; then
            echo -n '"stake_account":'`get_data_pubkey 264 "$ACCOUNT_DATA"`','
        fi

        echo -n '"stake_initial":'`to_sol \`get_data_u64 296 "$ACCOUNT_DATA"\``','

        echo -n '"stake_epoch":'`get_data_u64 304 "$ACCOUNT_DATA"`','

        echo -n '"last_harvest_ki_stake":'`to_sol \`get_data_u64 312 "$ACCOUNT_DATA"\``','

        echo -n '"last_commission_charge_stake":'`to_sol \`get_data_u64 320 "$ACCOUNT_DATA"\``

        echo -n '},'

        echo -n '"level":'`get_data_u8 328 "$ACCOUNT_DATA"`','

        echo -n '"metadata":{'

        echo -n '"level_1_ki":'`get_data_u32 332 "$ACCOUNT_DATA"`','

        echo -n '"random":['

        for i in `seq 1 15`; do
            echo -n `get_data_u32 $(($i*4+336)) "$ACCOUNT_DATA"`','
        done

        echo -n `get_data_u32 396 "$ACCOUNT_DATA"`'],"level_metadata":['

        for i in `seq 0 8`; do
            OFFSET=$(($i*292+400))
            echo -n '{"form":'`get_data_u32 $(($OFFSET+0)) "$ACCOUNT_DATA"`','

            echo -n '"skill":'`get_data_u8 $(($OFFSET+4)) "$ACCOUNT_DATA"`','

            echo -n '"ki_factor":'`get_data_u32 $(($OFFSET+8)) "$ACCOUNT_DATA"`','

            echo -n '"name":"'`get_data_string $(($OFFSET+16)) 48 "$ACCOUNT_DATA"`'",'

            echo -n '"uri":"'`get_data_string $(($OFFSET+64)) 200 "$ACCOUNT_DATA"`'",'

            echo -n '"uri_contents_sha256":"'`get_data_sha256 $(($OFFSET+264)) "$ACCOUNT_DATA"`'"'

            echo -n '}'

            if [ $i -lt 8 ]; then
                echo -n ','
            fi
        done            

        echo ']}}'

    ;;

    metaplex)

        shift
        
        if [ -z "$1" -o -z "$2" -o -z "$3" -o -n "$4" ]; then
            usage_exit
        fi

        BLOCK_PUBKEY=`pda $PROGRAM_PUBKEY u8[14] u32[$1] u32[$2]`

        MINT_PUBKEY=`pda $PROGRAM_PUBKEY u8[5] Pubkey[$BLOCK_PUBKEY] u16[$3]`

        METADATA_PUBKEY=`pda $METAPLEX_PROGRAM_PUBKEY String[metadata] Pubkey[$METAPLEX_PROGRAM_PUBKEY]               \
                                                      Pubkey[$MINT_PUBKEY]`

        ACCOUNT_DATA=`get_account_data $RPC_URL $METADATA_PUBKEY`

        if [ -z "$ACCOUNT_DATA" ]; then
            echo "Metaplex metadata account $1 $2 $3 does not exist"
            exit 1
        fi

        ACCOUNT_DATA_LEN=`echo "$ACCOUNT_DATA" | base64 -d | wc -c`
        REMAINING_DATA_LEN=$ACCOUNT_DATA_LEN

        # 1 byte -- Key
        if [ $REMAINING_DATA_LEN -lt 1 ]; then
            echo "Metaplex metadata account has invalid size $ACCOUNT_DATA_LEN"
            exit 1
        fi

        KEY=`get_data_u8 0 "$ACCOUNT_DATA"`
        REMAINING_DATA_LEN=$(($REMAINING_DATA_LEN-1))
        OFFSET=1
        
        if [ "0$KEY" -ne 4 ]; then # 4 is MetadataV1
           echo "Metaplex metadata account has invalid key $KEY"
           exit 1
        fi

        if [ $REMAINING_DATA_LEN -lt 64 ]; then
            echo "Metaplex metadata account has invalid size $ACCOUNT_DATA_LEN"
            exit 1
        fi

        # Update authority
        UPDATE_AUTHORITY=`get_data_pubkey $OFFSET "$ACCOUNT_DATA"`
        OFFSET=$(($OFFSET+32))

        MINT=`get_data_pubkey $OFFSET "$ACCOUNT_DATA"`
        OFFSET=$(($OFFSET+32))

        REMAINING_DATA_LEN=$(($REMAINING_DATA_LEN-64))

        if [ $(($REMAINING_DATA_LEN)) -lt 4 ]; then
            echo "Block account has invalid size $ACCOUNT_DATA_LEN"
            exit 1
        fi

        NAME_LEN=`get_data_u32 $OFFSET "$ACCOUNT_DATA"`
        REMAINING_DATA_LEN=$(($REMAINING_DATA_LEN-4))
        OFFSET=$(($OFFSET+4))

        if [ $REMAINING_DATA_LEN -lt $NAME_LEN ]; then
            echo "Block account has invalid size $ACCOUNT_DATA_LEN"
            exit 1
        fi

        if [ "0$NAME_LEN" -eq 0 ]; then
            NAME=""
        else
            NAME=`get_data_string $OFFSET $NAME_LEN "$ACCOUNT_DATA"`
        fi

        REMAINING_DATA_LEN=$(($REMAINING_DATA_LEN-$NAME_LEN))
        OFFSET=$(($OFFSET+$NAME_LEN))

        if [ $(($REMAINING_DATA_LEN)) -lt 4 ]; then
            echo "Block account has invalid size $ACCOUNT_DATA_LEN"
            exit 1
        fi
        
        SYMBOL_LEN=`get_data_u32 $OFFSET "$ACCOUNT_DATA"`
        REMAINING_DATA_LEN=$(($REMAINING_DATA_LEN-4))
        OFFSET=$(($OFFSET+4))

        if [ $REMAINING_DATA_LEN -lt $SYMBOL_LEN ]; then
            echo "Block account has invalid size $ACCOUNT_DATA_LEN"
            exit 1
        fi

        if [ "0$SYMBOL_LEN" -eq 0 ]; then
            SYMBOL=""
        else
            SYMBOL=`get_data_string $OFFSET $SYMBOL_LEN "$ACCOUNT_DATA"`
        fi

        REMAINING_DATA_LEN=$(($REMAINING_DATA_LEN-$SYMBOL_LEN))
        OFFSET=$(($OFFSET+$SYMBOL_LEN))

        if [ $(($REMAINING_DATA_LEN)) -lt 4 ]; then
            echo "Block account has invalid size $ACCOUNT_DATA_LEN"
            exit 1
        fi
        
        URI_LEN=`get_data_u32 $OFFSET "$ACCOUNT_DATA"`
        REMAINING_DATA_LEN=$(($REMAINING_DATA_LEN-4))
        OFFSET=$(($OFFSET+4))

        if [ $REMAINING_DATA_LEN -lt $URI_LEN ]; then
            echo "Block account has invalid size $ACCOUNT_DATA_LEN"
            exit 1
        fi

        if [ "0$URI_LEN" -eq 0 ]; then
            URI=""
        else
            URI=`get_data_string $OFFSET $URI_LEN "$ACCOUNT_DATA"`
        fi

        REMAINING_DATA_LEN=$(($REMAINING_DATA_LEN-$URI_LEN))
        OFFSET=$(($OFFSET+$URI_LEN))

        if [ $REMAINING_DATA_LEN -lt 2 ]; then
            echo "Block account has invalid size $ACCOUNT_DATA_LEN"
            exit 1
        fi

        SELLER_FEE_BASIS_POINTS=`get_data_u16 $OFFSET "$ACCOUNT_DATA"`
        REMAINING_DATA_LEN=$(($REMAINING_DATA_LEN-2))
        OFFSET=$(($OFFSET+2))

        # Creators vec
        if [ $REMAINING_DATA_LEN -lt 1 ]; then
            echo "Block account has invalid size $ACCOUNT_DATA_LEN"
            exit 1
        fi

        HAS_CREATORS=`get_data_u8 $OFFSET "$ACCOUNT_DATA"`
        REMAINING_DATA_LEN=$(($REMAINING_DATA_LEN-1))
        OFFSET=$(($OFFSET+1))

        CREATORS_JSON=""
        
        if [ "0$HAS_CREATORS" -ne 0 ]; then
            if [ $REMAINING_DATA_LEN -lt 4 ]; then
                echo "Block account has invalid size $ACCOUNT_DATA_LEN"
                exit 1
            fi
            
            CREATORS_COUNT=`get_data_u32 $OFFSET "$ACCOUNT_DATA"`
            REMAINING_DATA_LEN=$(($REMAINING_DATA_LEN-4))
            OFFSET=$(($OFFSET+4))

            if [ "0$CREATORS_COUNT" -ne 0 ]; then
                # Each Creator is 34 bytes long
                if [ $(($CREATORS_COUNT*34)) -gt $REMAINING_DATA_LEN ]; then
                    echo "Block account has invalid size $ACCOUNT_DATA_LEN"
                    exit 1
                fi
                CREATORS_JSON='"creators":['
                for i in `seq 1 $CREATORS_COUNT`; do
                    ADDRESS=`get_data_pubkey $OFFSET "$ACCOUNT_DATA"`
                    VERIFIED=`get_data_bool $(($OFFSET+32)) "$ACCOUNT_DATA"`
                    SHARE=`get_data_u8 $(($OFFSET+33)) "$ACCOUNT_DATA"`
                    if [ $i -gt 1 ]; then
                        CREATORS_JSON+=','
                    fi
                    CREATORS_JSON+='{"address":"'$ADDRESS'","verified":'$VERIFIED',"share":'$SHARE'}'
                    REMAINING_DATA_LEN=$(($REMAINING_DATA_LEN-34))
                    OFFSET=$(($OFFSET+34))
                done
                CREATORS_JSON+="]"
            fi
        fi

        if [ $REMAINING_DATA_LEN -lt 1 ]; then
            echo "Block account has invalid size $ACCOUNT_DATA_LEN"
            exit 1
        fi

        PRIMARY_SALE_HAPPENED=`get_data_bool $OFFSET "$ACCOUNT_DATA"`
        REMAINING_DATA_LEN=$(($REMAINING_DATA_LEN-1))
        OFFSET=$(($OFFSET+1))

        if [ $REMAINING_DATA_LEN -lt 1 ]; then
            echo "Block account has invalid size $ACCOUNT_DATA_LEN"
            exit 1
        fi

        IS_MUTABLE=`get_data_bool $OFFSET "$ACCOUNT_DATA"`
        REMAINING_DATA_LEN=$(($REMAINING_DATA_LEN-1))
        OFFSET=$(($OFFSET+1))
        
        if [ $REMAINING_DATA_LEN -lt 1 ]; then
            echo "Block account has invalid size $ACCOUNT_DATA_LEN"
            exit 1
        fi

        HAS_EDITION_NONCE=`get_data_u8 $OFFSET "$ACCOUNT_DATA"`
        REMAINING_DATA_LEN=$(($REMAINING_DATA_LEN-1))
        OFFSET=$(($OFFSET+1))

        EDITION_NONCE_JSON=

        if [ "0$HAS_EDITION_NONCE" -ne 0 ]; then
            if [ $REMAINING_DATA_LEN -lt 1 ]; then
                echo "Block account has invalid size $ACCOUNT_DATA_LEN"
                exit 1
            fi

            EDITION_NONCE_JSON='"edition_nonce":'`get_data_u8 $OFFSET "$ACCOUNT_DATA"`
            REMAINING_DATA_LEN=$(($REMAINING_DATA_LEN-1))
            OFFSET=$(($OFFSET+1))
        fi

        HAS_TOKEN_STANDARD=`get_data_u8 $OFFSET "$ACCOUNT_DATA"`
        REMAINING_DATA_LEN=$(($REMAINING_DATA_LEN-1))
        OFFSET=$(($OFFSET+1))

        TOKEN_STANDARD_JSON=

        if [ "0$HAS_TOKEN_STANDARD" -ne 0 ]; then
            if [ $REMAINING_DATA_LEN -lt 1 ]; then
                echo "Block account has invalid size $ACCOUNT_DATA_LEN"
                exit 1
            fi

            TOKEN_STANDARD=`get_data_u8 $OFFSET "$ACCOUNT_DATA"`
            REMAINING_DATA_LEN=$(($REMAINING_DATA_LEN-1))
            OFFSET=$(($OFFSET+1))

            TOKEN_STANDARD_JSON='"token_standard":"'

            case $TOKEN_STANDARD in
                0) TOKEN_STANDARD_JSON+="NonFungible" ;;
                1) TOKEN_STANDARD_JSON+="FungibleAsset" ;;
                2) TOKEN_STANDARD_JSON+="Fungible" ;;
                3) TOKEN_STANDARD_JSON+="NonFungibleEdition" ;;
                *) echo "Invalid token_standard $TOKEN_STANDARD"; exit 1 ;;
            esac

            TOKEN_STANDARD_JSON+='"'
        fi

        # Shinobi Immortals metadata never has collection or uses
        
        if [ $REMAINING_DATA_LEN -lt 2 ]; then
            echo "Metadata account has invalid size $ACCOUNT_DATA_LEN"
            exit 1
        fi

        if [ 0`get_data_u8 $OFFSET "$ACCOUNT_DATA"` -ne 0 ]; then
            echo "Metadata account has collection"
            exit 1
        fi

        if [ 0`get_data_u8 $(($OFFSET+1)) "$ACCOUNT_DATA"` -ne 0 ]; then
            echo "Metadata account has uses"
            exit 1
        fi

        echo -n '{"metadata_pubkey":"'$METADATA_PUBKEY'",'

        echo -n '"update_authority_pubkey":"'$UPDATE_AUTHORITY'",'

        echo -n '"mint_pubkey":"'$MINT_PUBKEY'",'

        echo -n '"name":"'$NAME'",'

        echo -n '"symbol":"'$SYMBOL'",'

        echo -n '"uri":"'$URI'",'

        echo -n '"seller_fee_basis_points":'$SELLER_FEE_BASIS_POINTS

        if [ -n "$CREATORS_JSON" ]; then
            echo -n ','$CREATORS_JSON
        fi

        echo -n ',"primary_sale_happened":'$PRIMARY_SALE_HAPPENED',"is_mutable":'$IS_MUTABLE

        if [ -n "$EDITION_NONCE_JSON" ]; then
            echo -n ','$EDITION_NONCE_JSON
        fi

        if [ $REMAINING_DATA_LEN -lt 1 ]; then
            echo "Block account has invalid size $ACCOUNT_DATA_LEN"
            exit 1
        fi

        if [ -n "$TOKEN_STANDARD_JSON" ]; then
            echo -n ','$TOKEN_STANDARD_JSON
        fi

        echo -n '}'
        
    ;;

    *)
        usage_exit
        ;;
esac
