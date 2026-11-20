/* Prints, as JSON, the bytes of instances of the program's account data structures (Block, Entry, ProgramState, and
 * Authority, from program/inc), filled with distinct values, together with the value of each field.  The fields are
 * named as the decoders of sdk/js/shinobi_immortals_base.js name them, so that test_program_consistency.js can decode
 * the bytes with those decoders and compare every field.  This checks the decoders' byte offsets against the
 * program's actual structure layouts, as the compiler lays them out.
 *
 * It is compiled for the host, which lays out these structures as the Solana BPF target does: both are 64 bit little
 * endian targets that align each integer type to its own size.  Usage: cc -I program -o layouts layouts.c; ./layouts
 *
 * Each structure is first filled with random bytes, so that padding between fields is not zero (as it need not be
 * on chain), and then every field that the decoders read is set.  Pubkeys are printed as "hex:" followed by their
 * bytes in hex; SHA-256 values as hex; strings as text; everything else as decimal.
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


// A fixed-seed xorshift generator, so that the output is the same on every run
static uint64_t g_state = 0x5348494e4f4249ull;

static uint64_t next_u64(void)
{
    g_state ^= g_state << 13;
    g_state ^= g_state >> 7;
    g_state ^= g_state << 17;
    return g_state;
}

static void fill_random(void *p, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        ((uint8_t *) p)[i] = (uint8_t) next_u64();
    }
}

static void random_pubkey(SolPubkey *pubkey)
{
    fill_random(pubkey->x, sizeof(pubkey->x));
}

// Sets a fixed size string field to a NUL terminated string of random letters, shorter than the field, leaving the
// random bytes after the terminator in place
static void random_string(uint8_t *field, size_t field_length)
{
    size_t length = 1 + (next_u64() % (field_length - 1));
    for (size_t i = 0; i < length; i++) {
        field[i] = 'a' + (next_u64() % 26);
    }
    field[length] = 0;
}


// JSON output: each object's members are separated by commas, which g_first tracks
static bool g_first;

static void begin(const char *name)
{
    printf("%s\"%s\":{", g_first ? "" : ",", name);
    g_first = true;
}

static void end(void)
{
    printf("}");
    g_first = false;
}

static void member_prefix(const char *name)
{
    printf("%s\"%s\":", g_first ? "" : ",", name);
    g_first = false;
}

static void print_u64(const char *name, uint64_t value)
{
    member_prefix(name);
    printf("\"%llu\"", (unsigned long long) value);
}

static void print_s64(const char *name, int64_t value)
{
    member_prefix(name);
    printf("\"%lld\"", (long long) value);
}

static void print_hex(const char *name, const char *prefix, const uint8_t *bytes, size_t length)
{
    member_prefix(name);
    printf("\"%s", prefix);
    for (size_t i = 0; i < length; i++) {
        printf("%02x", bytes[i]);
    }
    printf("\"");
}

static void print_pubkey(const char *name, const SolPubkey *pubkey)
{
    print_hex(name, "hex:", pubkey->x, sizeof(pubkey->x));
}

static void print_string(const char *name, const uint8_t *field)
{
    member_prefix(name);
    printf("\"%s\"", (const char *) field);
}

// Prints the bytes of a structure, and starts its "fields" object
static void begin_structure(const char *name, const void *p, size_t size)
{
    begin(name);
    print_hex("bytes", "", (const uint8_t *) p, size);
    begin("fields");
}

static void end_structure(void)
{
    end();
    end();
}


// Realistic timestamps (around the year 2026), which the decoders convert to Numbers
static int64_t random_timestamp(void)
{
    return 1700000000 + (int64_t) (next_u64() % 100000000);
}


static void block(void)
{
    static Block b;

    fill_random(&b, sizeof(b));

    b.data_type = DataType_Block;
    b.config.group_number = (uint32_t) next_u64();
    b.config.block_number = (uint32_t) next_u64();
    b.config.total_entry_count = (uint16_t) next_u64();
    b.config.total_mystery_count = (uint16_t) next_u64();
    b.config.mystery_phase_duration = (uint32_t) next_u64();
    b.config.mystery_start_price_lamports = next_u64();
    b.config.reveal_period_duration = (uint32_t) next_u64();
    b.config.minimum_price_lamports = next_u64();
    b.config.has_auction = true;
    b.config.duration = (uint32_t) next_u64();
    b.config.final_start_price_lamports = next_u64();
    b.config.whitelist_duration = (uint32_t) next_u64();
    b.added_entries_count = (uint16_t) next_u64();
    b.block_start_timestamp = random_timestamp();
    b.mysteries_sold_count = (uint16_t) next_u64();
    b.mystery_phase_end_timestamp = random_timestamp();

    begin_structure("block", &b, sizeof(b));
    print_u64("group_number", b.config.group_number);
    print_u64("block_number", b.config.block_number);
    print_u64("total_entry_count", b.config.total_entry_count);
    print_u64("total_mystery_count", b.config.total_mystery_count);
    print_u64("mystery_phase_duration", b.config.mystery_phase_duration);
    print_u64("mystery_start_price_lamports", b.config.mystery_start_price_lamports);
    print_u64("reveal_period_duration", b.config.reveal_period_duration);
    print_u64("minimum_price_lamports", b.config.minimum_price_lamports);
    print_u64("has_auction", b.config.has_auction);
    print_u64("duration", b.config.duration);
    print_u64("non_auction_start_price_lamports", b.config.final_start_price_lamports);
    print_u64("whitelist_duration", b.config.whitelist_duration);
    print_u64("added_entries_count", b.added_entries_count);
    print_s64("block_start_timestamp", b.block_start_timestamp);
    print_u64("mysteries_sold_count", b.mysteries_sold_count);
    print_s64("mystery_phase_end_timestamp", b.mystery_phase_end_timestamp);
    end_structure();
}


static void entry(void)
{
    static Entry e;

    fill_random(&e, sizeof(e));

    e.data_type = DataType_Entry;
    e.group_number = (uint32_t) next_u64();
    e.block_number = (uint32_t) next_u64();
    e.entry_index = (uint16_t) next_u64();
    random_pubkey(&e.mint_pubkey);
    random_pubkey(&e.token_pubkey);
    random_pubkey(&e.metaplex_metadata_pubkey);
    e.minimum_price_lamports = next_u64();
    e.has_auction = true;
    e.duration = (uint32_t) next_u64();
    e.non_auction_start_price_lamports = next_u64();
    fill_random(&e.reveal_sha256, sizeof(e.reveal_sha256));
    e.reveal_timestamp = random_timestamp();
    e.purchase_price_lamports = next_u64();
    e.refund_awarded = true;
    e.reauthorized = true;
    e.auction.highest_bid_lamports = next_u64();
    random_pubkey(&e.auction.winning_bid_pubkey);
    random_pubkey(&e.owned.stake_account);
    e.owned.last_ki_harvest_stake_account_lamports = next_u64();
    e.level = (uint8_t) (next_u64() % 9);
    e.metadata.level_1_credits = (uint32_t) next_u64();
    for (unsigned i = 0; i < ARRAY_LEN(e.metadata.random); i++) {
        e.metadata.random[i] = (uint32_t) next_u64();
    }
    for (unsigned i = 0; i < ARRAY_LEN(e.metadata.level_metadata); i++) {
        LevelMetadata *m = &(e.metadata.level_metadata[i]);
        m->form = (uint32_t) next_u64();
        m->skill = (uint8_t) next_u64();
        m->ki_factor = (uint32_t) next_u64();
        random_string(m->name, sizeof(m->name));
        random_string(m->title, sizeof(m->title));
        random_string(m->uri, sizeof(m->uri));
        fill_random(&m->uri_contents_sha256, sizeof(m->uri_contents_sha256));
    }
    e.metadata.expected_annual_reward_bps = (uint32_t) next_u64();
    e.metadata.ki_harvest_reduction_per_day = (uint32_t) next_u64();
    e.most_recent_ki_applied_window = next_u64();
    e.ki_applied_this_window = next_u64();
    e.total_level_credits = next_u64();
    e.epochs_staked = next_u64();
    e.epochs_staked_counted_to_epoch = next_u64();

    begin_structure("entry", &e, sizeof(e));
    print_u64("group_number", e.group_number);
    print_u64("block_number", e.block_number);
    print_u64("entry_index", e.entry_index);
    print_pubkey("mint_address", &e.mint_pubkey);
    print_pubkey("token_address", &e.token_pubkey);
    print_pubkey("metaplex_metadata_address", &e.metaplex_metadata_pubkey);
    print_u64("minimum_price_lamports", e.minimum_price_lamports);
    print_u64("has_auction", e.has_auction);
    print_u64("duration", e.duration);
    print_u64("non_auction_start_price_lamports", e.non_auction_start_price_lamports);
    print_hex("reveal_sha256", "", e.reveal_sha256.x, sizeof(e.reveal_sha256.x));
    print_s64("reveal_timestamp", e.reveal_timestamp);
    print_u64("purchase_price_lamports", e.purchase_price_lamports);
    print_u64("refund_awarded", e.refund_awarded);
    print_u64("reauthorized", e.reauthorized);
    print_u64("auction_highest_bid_lamports", e.auction.highest_bid_lamports);
    print_pubkey("auction_winning_bid_address", &e.auction.winning_bid_pubkey);
    print_pubkey("owned_stake_account", &e.owned.stake_account);
    print_u64("owned_last_ki_harvest_stake_account_lamports", e.owned.last_ki_harvest_stake_account_lamports);
    print_u64("level", e.level);
    print_u64("most_recent_ki_applied_window", e.most_recent_ki_applied_window);
    print_u64("ki_applied_this_window", e.ki_applied_this_window);
    print_u64("total_level_credits", e.total_level_credits);
    print_u64("epochs_staked", e.epochs_staked);
    print_u64("epochs_staked_counted_to_epoch", e.epochs_staked_counted_to_epoch);
    print_u64("metadata_level_1_credits", e.metadata.level_1_credits);
    // metadata_random is an array of 16 values, and level_metadata an array of 9 objects
    member_prefix("metadata_random");
    printf("[");
    for (unsigned i = 0; i < ARRAY_LEN(e.metadata.random); i++) {
        printf("%s\"%u\"", i ? "," : "", e.metadata.random[i]);
    }
    printf("]");
    member_prefix("level_metadata");
    printf("[");
    for (unsigned i = 0; i < ARRAY_LEN(e.metadata.level_metadata); i++) {
        const LevelMetadata *m = &(e.metadata.level_metadata[i]);
        printf("%s{", i ? "," : "");
        g_first = true;
        print_u64("form", m->form);
        print_u64("skill", m->skill);
        print_u64("ki_factor", m->ki_factor);
        print_string("name", m->name);
        print_string("title", m->title);
        print_string("uri", m->uri);
        print_hex("uri_contents_sha256", "", m->uri_contents_sha256.x, sizeof(m->uri_contents_sha256.x));
        printf("}");
    }
    printf("]");
    g_first = false;
    print_u64("metadata_expected_annual_reward_bps", e.metadata.expected_annual_reward_bps);
    print_u64("metadata_ki_harvest_reduction_per_day", e.metadata.ki_harvest_reduction_per_day);
    end_structure();
}


static void program_state(void)
{
    static ProgramState s;

    fill_random(&s, sizeof(s));

    s.data_type = DataType_ProgramState;
    random_pubkey(&s.admin_pubkey);

    begin_structure("program_state", &s, sizeof(s));
    print_pubkey("admin_address", &s.admin_pubkey);
    end_structure();
}


static void authority(void)
{
    static Authority a;

    fill_random(&a, sizeof(a));

    a.data_type = DataType_Authority;
    a.mystery_escrow_lamports = next_u64();

    begin_structure("authority", &a, sizeof(a));
    print_u64("mystery_escrow_lamports", a.mystery_escrow_lamports);
    end_structure();
}


int main(void)
{
    printf("{");
    g_first = true;
    block();
    entry();
    program_state();
    authority();
    // The data type values that the decoders check
    begin("data_types");
    print_u64("DataType_Authority", DataType_Authority);
    end();
    printf("}\n");
    return 0;
}
