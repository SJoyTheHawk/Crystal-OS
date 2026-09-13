/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/*
 * Host-side tests for the pieces that must not be debugged on a display.
 *
 *   cc -I../include -I. -o /tmp/t test_transit_json.c ../src/transit_json.c \
 *      ../src/transit_routes.c ../src/transit_index_data.c && /tmp/t
 *
 * transit_routes.c calls transit_index_load(), which is stubbed here: the
 * persisted-index path needs SPIFFS and belongs on device.
 */

#include <stdio.h>
#include <string.h>

#include "transit_json.h"
#include "transit_routes.h"

void transit_index_load(void) {}

static int g_failures;

#define CHECK(condition, ...) do { \
    if (!(condition)) { printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); \
                        printf("\n"); ++g_failures; } \
} while (0)

// ------------------------------------------------------------------ collector

#define MAX_RECORDS 300

typedef struct {
    char stop[32];
    char seq[8];
    char eta[40];
    char name[64];
    bool truncated;
} record_t;

typedef struct {
    record_t records[MAX_RECORDS];
    size_t   count;
    record_t current;
} collector_t;

static bool collect_member(const transit_json_member_t *member, void *user_data)
{
    collector_t *collector = user_data;
    if (strcmp(member->key, "stop") == 0) snprintf(collector->current.stop, sizeof(collector->current.stop), "%s", member->value);
    else if (strcmp(member->key, "seq") == 0) snprintf(collector->current.seq, sizeof(collector->current.seq), "%s", member->value);
    else if (strcmp(member->key, "eta") == 0) snprintf(collector->current.eta, sizeof(collector->current.eta), "%s", member->value);
    else if (strcmp(member->key, "name_en") == 0) {
        snprintf(collector->current.name, sizeof(collector->current.name), "%s", member->value);
        collector->current.truncated = member->truncated;
    }
    return true;
}

static bool collect_object_end(uint8_t depth, bool in_array, void *user_data)
{
    (void)depth;
    collector_t *collector = user_data;
    if (!in_array) return true;
    if (collector->count < MAX_RECORDS) collector->records[collector->count++] = collector->current;
    memset(&collector->current, 0, sizeof(collector->current));
    return true;
}

// Feeds a document in fixed-size slices, so every chunk boundary is exercised.
static bool parse_in_chunks(const char *document, size_t chunk, collector_t *collector)
{
    memset(collector, 0, sizeof(*collector));
    transit_json_parser_t parser;
    transit_json_init(&parser, collect_member, collect_object_end, collector);
    const size_t length = strlen(document);
    for (size_t offset = 0; offset < length; offset += chunk) {
        size_t take = length - offset;
        if (take > chunk) take = chunk;
        if (!transit_json_feed(&parser, document + offset, take)) break;
    }
    return transit_json_finish(&parser) && !transit_json_failed(&parser);
}

// ---------------------------------------------------------------------- tests

// The real shape of a KMB route-stop response, trimmed to three stops.
static const char kRouteStop[] =
    "{\"type\":\"RouteStop\",\"version\":\"1.0\","
    "\"generated_timestamp\":\"2026-09-12T13:56:44+08:00\",\"data\":["
    "{\"route\":\"68X\",\"bound\":\"O\",\"service_type\":\"1\",\"seq\":\"1\",\"stop\":\"6D66CBA494DECB08\"},"
    "{\"route\":\"68X\",\"bound\":\"O\",\"service_type\":\"1\",\"seq\":\"2\",\"stop\":\"EF43A73D4C0CC508\"},"
    "{\"route\":\"68X\",\"bound\":\"O\",\"service_type\":\"1\",\"seq\":\"3\",\"stop\":\"BB4F24A4C6A9B222\"}]}";

// CTB sends seq as a bare number and pretty-prints with spaces.
static const char kCtbRouteStop[] =
    "{\"type\": \"RouteStop\", \"version\": \"2.0\", \"data\": ["
    "{\"co\": \"CTB\", \"route\": \"962X\", \"dir\": \"O\", \"seq\": 1, \"stop\": \"001939\"}, "
    "{\"co\": \"CTB\", \"route\": \"962X\", \"dir\": \"O\", \"seq\": 12, \"stop\": \"001935\"}]}";

static void test_chunk_boundaries(void)
{
    // Every chunk size from 1 byte upward must produce identical records. A key
    // or value straddling a boundary is the failure this catches.
    for (size_t chunk = 1; chunk <= 64; ++chunk) {
        collector_t collector;
        const bool ok = parse_in_chunks(kRouteStop, chunk, &collector);
        CHECK(ok, "chunk %zu: parse failed", chunk);
        CHECK(collector.count == 3, "chunk %zu: got %zu records", chunk, collector.count);
        if (collector.count == 3) {
            CHECK(strcmp(collector.records[0].stop, "6D66CBA494DECB08") == 0,
                  "chunk %zu: stop 0 = %s", chunk, collector.records[0].stop);
            CHECK(strcmp(collector.records[2].stop, "BB4F24A4C6A9B222") == 0,
                  "chunk %zu: stop 2 = %s", chunk, collector.records[2].stop);
            CHECK(strcmp(collector.records[1].seq, "2") == 0,
                  "chunk %zu: seq 1 = %s", chunk, collector.records[1].seq);
        }
    }
}

static void test_ctb_numeric_seq(void)
{
    for (size_t chunk = 1; chunk <= 32; ++chunk) {
        collector_t collector;
        const bool ok = parse_in_chunks(kCtbRouteStop, chunk, &collector);
        CHECK(ok, "ctb chunk %zu: parse failed", chunk);
        CHECK(collector.count == 2, "ctb chunk %zu: got %zu", chunk, collector.count);
        if (collector.count == 2) {
            CHECK(strcmp(collector.records[1].seq, "12") == 0,
                  "ctb chunk %zu: seq = %s", chunk, collector.records[1].seq);
            CHECK(strcmp(collector.records[1].stop, "001935") == 0,
                  "ctb chunk %zu: stop = %s", chunk, collector.records[1].stop);
        }
    }
}

static void test_truncated_body_fails(void)
{
    // A body cut mid-array must fail the whole request, not yield partial records.
    char truncated[sizeof(kRouteStop)];
    snprintf(truncated, sizeof(truncated), "%.*s", 120, kRouteStop);
    collector_t collector;
    const bool ok = parse_in_chunks(truncated, 16, &collector);
    CHECK(!ok, "truncated body reported success");
}

static void test_malformed_fails(void)
{
    const char *bad[] = {
        "{\"data\":[}]}",            // mismatched close
        "{\"data\":[{\"a\":1}",      // unclosed array
        "{\"a\":\"unterminated",     // unclosed string
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        collector_t collector;
        CHECK(!parse_in_chunks(bad[i], 4, &collector), "malformed %zu accepted", i);
    }
}

static void test_escapes_and_truncation(void)
{
    // A quote inside a name must not end the string early, and an over-long value
    // must truncate rather than corrupt.
    const char *document =
        "{\"data\":[{\"name_en\":\"Kwai \\\"Chung\\\" Rd\\/Lane\"},"
        "{\"name_en\":\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\"}]}";
    collector_t collector;
    CHECK(parse_in_chunks(document, 7, &collector), "escape document failed");
    CHECK(collector.count == 2, "escape document: %zu records", collector.count);
    if (collector.count == 2) {
        CHECK(strcmp(collector.records[0].name, "Kwai \"Chung\" Rd/Lane") == 0,
              "escapes: %s", collector.records[0].name);
        CHECK(collector.records[1].truncated, "over-long value not flagged truncated");
        CHECK(strlen(collector.records[1].name) < 64, "over-long value overflowed");
    }
}

static void test_null_eta(void)
{
    // A scheduled departure with no live time sends eta:null. It must be readable
    // as a literal, not mistaken for a string.
    const char *document = "{\"data\":[{\"eta\":null,\"rmk_en\":\"Scheduled\"},"
                           "{\"eta\":\"2026-09-12T14:32:00+08:00\"}]}";
    collector_t collector;
    CHECK(parse_in_chunks(document, 5, &collector), "null eta document failed");
    CHECK(collector.count == 2, "null eta: %zu records", collector.count);
    if (collector.count == 2) {
        CHECK(strcmp(collector.records[0].eta, "null") == 0, "eta 0 = %s", collector.records[0].eta);
        CHECK(strcmp(collector.records[1].eta, "2026-09-12T14:32:00+08:00") == 0,
              "eta 1 = %s", collector.records[1].eta);
    }
}

static void test_iso8601(void)
{
    // +08:00 is what both feeds send. The epoch must not depend on the device's
    // local timezone, which is what mktime() would have introduced.
    const int32_t hk = transit_parse_iso8601("2026-09-12T14:32:00+08:00");
    const int32_t utc = transit_parse_iso8601("2026-09-12T06:32:00+00:00");
    const int32_t zulu = transit_parse_iso8601("2026-09-12T06:32:00Z");
    CHECK(hk == utc, "offset ignored: %ld vs %ld", (long)hk, (long)utc);
    CHECK(hk == zulu, "Z not treated as UTC: %ld vs %ld", (long)hk, (long)zulu);
    // 2026-09-12T06:32:00Z, cross-checked against Python's datetime.
    CHECK(hk == 1789194720L, "epoch = %ld", (long)hk);

    // One minute apart must be exactly 60 seconds apart.
    CHECK(transit_parse_iso8601("2026-09-12T14:33:00+08:00") - hk == 60, "minute arithmetic");
    CHECK(transit_parse_iso8601("garbage") == 0, "garbage accepted");
    CHECK(transit_parse_iso8601("") == 0, "empty accepted");
    CHECK(transit_parse_iso8601("2026-09-12T14:32:00.123+08:00") == hk, "fractional seconds");
}

static void test_route_index(void)
{
    const char *charset = transit_route_charset();

    // Exact names that exist on both operators, and one that exists on neither.
    CHECK(transit_route_is_complete("68X", 3) != 0, "68X not complete");
    CHECK(transit_route_is_complete("1", 1) != 0, "1 not complete");
    CHECK(transit_route_is_complete("68Y", 3) == 0, "68Y reported complete");
    CHECK(transit_route_is_complete("", 0) == 0, "empty reported complete");
    CHECK(transit_route_is_complete("68XXX", 5) == 0, "over-long reported complete");

    // After "68" the mask must contain the characters that really follow.
    const uint32_t mask = transit_route_next_mask("68", 2);
    const char *x = strchr(charset, 'X');
    const char *a = strchr(charset, 'A');
    CHECK(x != NULL && (mask & (1u << (x - charset))), "68 -> X not lit");
    CHECK(a != NULL && (mask & (1u << (a - charset))), "68 -> A not lit");

    // A full-length prefix has nothing that can follow it.
    CHECK(transit_route_next_mask("68XA", 4) == 0, "full-length prefix returned a mask");
    CHECK(transit_route_next_mask(NULL, 0) == 0, "NULL prefix returned a mask");

    // KMB variants: 68X runs outbound and inbound at service type 1.
    transit_route_variant_t variants[TRANSIT_ROUTE_NAME_LEN * 2];
    const size_t count = transit_route_kmb_variants("68X", 3, variants,
                                                    sizeof(variants) / sizeof(variants[0]));
    CHECK(count >= 2, "68X variants = %zu", count);
    bool outbound = false, inbound = false;
    for (size_t i = 0; i < count; ++i) {
        if (variants[i].bound == 'O') outbound = true;
        if (variants[i].bound == 'I') inbound = true;
        CHECK(variants[i].service_type >= 1, "service_type %u", variants[i].service_type);
    }
    CHECK(outbound && inbound, "68X missing a direction");

    // A CTB-only route has no KMB variants, which is what makes the service skip
    // the KMB endpoints for it rather than fetching 422s.
    CHECK(transit_route_kmb_variants("962X", 4, variants,
                                     sizeof(variants) / sizeof(variants[0])) == 0,
          "962X reported KMB variants");
    CHECK(transit_route_is_complete("962X", 4) != 0, "962X not complete");

    // The whole table must be sorted, since every lookup binary-searches it.
    for (uint16_t i = 1; i < transit_route_index_count; ++i) {
        if (memcmp(transit_route_index[i - 1].name, transit_route_index[i].name,
                   TRANSIT_ROUTE_NAME_LEN) >= 0) {
            CHECK(false, "index not sorted at %u", (unsigned)i);
            break;
        }
    }
    // Every name must be inside the charset the keypad draws.
    for (uint16_t i = 0; i < transit_route_index_count; ++i) {
        for (int c = 0; c < TRANSIT_ROUTE_NAME_LEN; ++c) {
            const char ch = transit_route_index[i].name[c];
            if (ch == ' ') continue;
            if (strchr(charset, ch) == NULL) {
                CHECK(false, "name %u has %c outside charset", (unsigned)i, ch);
                break;
            }
        }
    }
    // Variant name_index values must be in range, or a lookup reads past the end.
    for (uint16_t i = 0; i < transit_route_variant_count; ++i) {
        if (transit_route_variants[i].name_index >= transit_route_index_count) {
            CHECK(false, "variant %u has out-of-range name_index", (unsigned)i);
            break;
        }
    }
}

static void test_depth_limit(void)
{
    // Deeply nested input must fail cleanly rather than overrun the depth array.
    char deep[128];
    memset(deep, '[', sizeof(deep) - 1);
    deep[sizeof(deep) - 1] = '\0';
    collector_t collector;
    CHECK(!parse_in_chunks(deep, 3, &collector), "deep nesting accepted");
}

int main(void)
{
    test_chunk_boundaries();
    test_ctb_numeric_seq();
    test_truncated_body_fails();
    test_malformed_fails();
    test_escapes_and_truncation();
    test_null_eta();
    test_iso8601();
    test_route_index();
    test_depth_limit();

    if (g_failures == 0) printf("all transit tests passed\n");
    else printf("%d check(s) failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
