/* Prints, as JSON, the layout of every structure of the program's accounts (from program/inc), as the C compiler lays
 * them out, for tests/layouts.rs to compare with the Rust crate's structures (sdk/rust/src/state.rs).
 *
 * For each structure, it prints the structure's size and alignment, the bytes of an instance filled with random bytes
 * (so that every field, and the padding between fields, holds a distinctive value; each signed field is made negative,
 * so that its signedness is tested too), and for each field its name, offset,
 * size, and value in that instance: integers as decimal, with the signedness of their C type, and arrays and addresses
 * as hex.  A field that is itself a structure (or an array of structures) has no value; its own fields are given with
 * its structure.
 *
 * It is compiled for the host, which lays out these structures as the Solana BPF target does: both are 64 bit little
 * endian targets that align each integer type to its own size.  Usage: cc -I program -o layouts layouts.c; ./layouts
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// The only type that the program's headers need from the Solana SDK
typedef struct { uint8_t x[32]; } SolPubkey;

#include "inc/types.h"
#include "inc/data_type.h"
#include "inc/block.h"
#include "inc/entry.h"
#include "inc/program_state.h"
#include "inc/authority.h"
#include "inc/bid.h"
#include "inc/whitelist.h"


// A fixed-seed xorshift generator, so that the output is the same on every run
static uint64_t g_state = 0x5348494e4f4249ull;

static void fill_random(void *p, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        g_state ^= g_state << 13;
        g_state ^= g_state >> 7;
        g_state ^= g_state << 17;
        ((uint8_t *) p)[i] = (uint8_t) g_state;
    }
}

static void print_hex(const void *p, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        printf("%02x", ((const uint8_t *) p)[i]);
    }
}


// Whether a structure or field printed so far needs a comma before the next
static bool g_first_structure = true;
static bool g_first_field;

// The kinds of field value: unsigned and signed integers, bytes (arrays and addresses), and structures
enum Kind { Unsigned, Signed, Bytes, Structure };

static void print_field(const char *name, size_t offset, size_t size, enum Kind kind, const void *value)
{
    printf("%s{\"name\":\"%s\",\"offset\":%zu,\"size\":%zu", g_first_field ? "" : ",", name, offset, size);
    g_first_field = false;

    uint64_t u = 0;
    if ((kind == Unsigned) || (kind == Signed)) {
        // Little-endian, as both the host and the program's target are
        memcpy(&u, value, size);
    }
    switch (kind) {
    case Unsigned:
        printf(",\"value\":\"%llu\"", (unsigned long long) u);
        break;
    case Signed:
        // Sign extend from the field's size
        if ((size < 8) && (u & (1ull << ((size * 8) - 1)))) {
            u |= ~0ull << (size * 8);
        }
        printf(",\"value\":\"%lld\"", (long long) u);
        break;
    case Bytes:
        printf(",\"value\":\"");
        print_hex(value, size);
        printf("\"");
        break;
    case Structure:
        break;
    }
    printf("}");
}

// Declares an instance of a structure, filled with random bytes
#define DECLARE(type, var)                                                                                          \
    type var;                                                                                                       \
    fill_random(&var, sizeof(var))

// Makes a signed field of an instance negative, so that reading it as unsigned would give a different value
#define NEGATIVE(var, field) var.field = -1 - (var.field & INT64_MAX)

// Begins the output of a structure, declared with DECLARE: its size, alignment, and bytes
#define BEGIN(type, var)                                                                                            \
    printf("%s\"%s\":{\"size\":%zu,\"align\":%zu,\"bytes\":\"", g_first_structure ? "" : ",", #type, sizeof(type),   \
           _Alignof(type));                                                                                         \
    g_first_structure = false;                                                                                      \
    print_hex(&var, sizeof(var));                                                                                   \
    printf("\",\"fields\":[");                                                                                      \
    g_first_field = true

// A field of the structure begun with BEGIN; field may name a member of a member (such as auction.winning_bid_pubkey)
#define FIELD(type, var, field, kind) print_field(#field, offsetof(type, field), sizeof(var.field), kind, &var.field)

#define END() printf("]}")


int main(void)
{
    printf("{");

    DECLARE(ProgramState, program_state);
    BEGIN(ProgramState, program_state);
    FIELD(ProgramState, program_state, data_type, Unsigned);
    FIELD(ProgramState, program_state, admin_pubkey, Bytes);
    END();

    DECLARE(Authority, authority);
    BEGIN(Authority, authority);
    FIELD(Authority, authority, data_type, Unsigned);
    FIELD(Authority, authority, mystery_escrow_lamports, Unsigned);
    END();

    DECLARE(BlockConfiguration, config);
    BEGIN(BlockConfiguration, config);
    FIELD(BlockConfiguration, config, group_number, Unsigned);
    FIELD(BlockConfiguration, config, block_number, Unsigned);
    FIELD(BlockConfiguration, config, total_entry_count, Unsigned);
    FIELD(BlockConfiguration, config, total_mystery_count, Unsigned);
    FIELD(BlockConfiguration, config, mystery_phase_duration, Unsigned);
    FIELD(BlockConfiguration, config, mystery_start_price_lamports, Unsigned);
    FIELD(BlockConfiguration, config, reveal_period_duration, Unsigned);
    FIELD(BlockConfiguration, config, minimum_price_lamports, Unsigned);
    FIELD(BlockConfiguration, config, has_auction, Unsigned);
    FIELD(BlockConfiguration, config, duration, Unsigned);
    FIELD(BlockConfiguration, config, final_start_price_lamports, Unsigned);
    FIELD(BlockConfiguration, config, whitelist_duration, Unsigned);
    FIELD(BlockConfiguration, config, expected_annual_reward_bps, Unsigned);
    END();

    DECLARE(Block, block);
    NEGATIVE(block, block_start_timestamp);
    NEGATIVE(block, mystery_phase_end_timestamp);
    BEGIN(Block, block);
    FIELD(Block, block, data_type, Unsigned);
    FIELD(Block, block, config, Structure);
    FIELD(Block, block, added_entries_count, Unsigned);
    FIELD(Block, block, block_start_timestamp, Signed);
    FIELD(Block, block, mysteries_sold_count, Unsigned);
    FIELD(Block, block, mystery_phase_end_timestamp, Signed);
    END();

    DECLARE(LevelMetadata, level_metadata);
    BEGIN(LevelMetadata, level_metadata);
    FIELD(LevelMetadata, level_metadata, form, Unsigned);
    FIELD(LevelMetadata, level_metadata, skill, Unsigned);
    FIELD(LevelMetadata, level_metadata, ki_factor, Unsigned);
    FIELD(LevelMetadata, level_metadata, name, Bytes);
    FIELD(LevelMetadata, level_metadata, title, Bytes);
    FIELD(LevelMetadata, level_metadata, uri, Bytes);
    FIELD(LevelMetadata, level_metadata, uri_contents_sha256, Bytes);
    END();

    DECLARE(EntryMetadata, entry_metadata);
    BEGIN(EntryMetadata, entry_metadata);
    FIELD(EntryMetadata, entry_metadata, level_1_credits, Unsigned);
    FIELD(EntryMetadata, entry_metadata, random, Bytes);
    FIELD(EntryMetadata, entry_metadata, level_metadata, Structure);
    FIELD(EntryMetadata, entry_metadata, ki_harvest_reduction_per_day, Unsigned);
    END();

    DECLARE(Entry, entry);
    NEGATIVE(entry, reveal_timestamp);
    BEGIN(Entry, entry);
    FIELD(Entry, entry, data_type, Unsigned);
    FIELD(Entry, entry, block_pubkey, Bytes);
    FIELD(Entry, entry, group_number, Unsigned);
    FIELD(Entry, entry, block_number, Unsigned);
    FIELD(Entry, entry, entry_index, Unsigned);
    FIELD(Entry, entry, mint_pubkey, Bytes);
    FIELD(Entry, entry, token_pubkey, Bytes);
    FIELD(Entry, entry, metaplex_metadata_pubkey, Bytes);
    FIELD(Entry, entry, minimum_price_lamports, Unsigned);
    FIELD(Entry, entry, has_auction, Unsigned);
    FIELD(Entry, entry, duration, Unsigned);
    FIELD(Entry, entry, non_auction_start_price_lamports, Unsigned);
    FIELD(Entry, entry, reveal_sha256, Bytes);
    FIELD(Entry, entry, reveal_timestamp, Signed);
    FIELD(Entry, entry, purchase_price_lamports, Unsigned);
    FIELD(Entry, entry, refund_awarded, Unsigned);
    FIELD(Entry, entry, reauthorized, Unsigned);
    FIELD(Entry, entry, auction, Structure);
    FIELD(Entry, entry, auction.highest_bid_lamports, Unsigned);
    FIELD(Entry, entry, auction.winning_bid_pubkey, Bytes);
    FIELD(Entry, entry, owned, Structure);
    FIELD(Entry, entry, owned.stake_account, Bytes);
    FIELD(Entry, entry, owned.last_ki_harvest_stake_account_lamports, Unsigned);
    FIELD(Entry, entry, level, Unsigned);
    FIELD(Entry, entry, metadata, Structure);
    FIELD(Entry, entry, most_recent_ki_applied_window, Unsigned);
    FIELD(Entry, entry, ki_applied_this_window, Unsigned);
    FIELD(Entry, entry, total_level_credits, Unsigned);
    FIELD(Entry, entry, epochs_staked, Unsigned);
    FIELD(Entry, entry, epochs_staked_counted_to_epoch, Unsigned);
    END();

    DECLARE(Bid, bid);
    BEGIN(Bid, bid);
    FIELD(Bid, bid, data_type, Unsigned);
    FIELD(Bid, bid, mint_pubkey, Bytes);
    FIELD(Bid, bid, bidder_pubkey, Bytes);
    END();

    DECLARE(Whitelist, whitelist);
    BEGIN(Whitelist, whitelist);
    FIELD(Whitelist, whitelist, data_type, Unsigned);
    FIELD(Whitelist, whitelist, count, Unsigned);
    FIELD(Whitelist, whitelist, entries, Bytes);
    END();

    printf("}\n");

    return 0;
}
