/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once

// Bounded incremental JSON scanner.
//
// Not cJSON: a DOM over the 349 KB route table is the 600 KB-1.5 MB failure mode
// the evaluation flagged. Not Weather's strstr approach either: that works on a
// 1 KB document with unique keys, but on an array of objects it cannot tell
// which object a key belongs to, which is exactly the bug it would introduce
// here.
//
// The scanner is fed arbitrary chunks -- a key or value may straddle a chunk
// boundary -- and emits (depth, key, value) for scalar members as they close.
// Values longer than their destination are truncated, never allocated.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TRANSIT_JSON_KEY_MAX   24
#define TRANSIT_JSON_VALUE_MAX 96
#define TRANSIT_JSON_DEPTH_MAX 8

typedef enum {
    TRANSIT_JSON_STRING = 0,
    TRANSIT_JSON_NUMBER,
    TRANSIT_JSON_LITERAL,   // true / false / null
} transit_json_value_type_t;

typedef struct {
    uint8_t                  depth;        // container depth of the member
    bool                     in_array;     // innermost container is an array
    const char              *key;          // "" for array elements
    const char              *value;        // NUL-terminated, possibly truncated
    bool                     truncated;
    transit_json_value_type_t type;
} transit_json_member_t;

// Called for every scalar member. Return false to stop the parse early.
typedef bool (*transit_json_cb_t)(const transit_json_member_t *member, void *user_data);

// Called when an object closes, so a consumer can commit a record.
//
// `in_array` is true when the object that just closed was an element of an array,
// which is what identifies a record in a "data":[...] list. Consumers key on
// that rather than on a literal depth: the two feeds nest their envelopes
// differently, and a hard-coded depth silently collects nothing when one of them
// changes shape.
typedef bool (*transit_json_object_end_cb_t)(uint8_t depth, bool in_array, void *user_data);

typedef struct {
    transit_json_cb_t            on_member;
    transit_json_object_end_cb_t on_object_end;
    void                        *user_data;

    // Parser state. Zero-initialise before the first chunk.
    uint8_t depth;
    bool    is_array[TRANSIT_JSON_DEPTH_MAX];
    bool    in_string;
    bool    escaped;
    bool    is_key;             // the current string is a key
    bool    have_key;           // a key has been captured at this level
    bool    in_scalar;          // accumulating a bare number/literal
    bool    string_was_value;   // the string just closed was a value
    bool    failed;
    bool    stopped;
    bool    overflow;           // the current token outgrew its buffer

    char   key[TRANSIT_JSON_DEPTH_MAX][TRANSIT_JSON_KEY_MAX];
    char   token[TRANSIT_JSON_VALUE_MAX];
    size_t token_len;
} transit_json_parser_t;

void transit_json_init(transit_json_parser_t *parser, transit_json_cb_t on_member,
                       transit_json_object_end_cb_t on_object_end, void *user_data);

// Feeds one chunk. Returns false once the document is malformed or a callback
// asked to stop; check transit_json_failed() to tell those apart.
bool transit_json_feed(transit_json_parser_t *parser, const char *data, size_t length);

// True when the document did not parse. A stopped parse is not a failure.
bool transit_json_failed(const transit_json_parser_t *parser);

// True when a callback ended the parse early.
bool transit_json_stopped(const transit_json_parser_t *parser);

// Finishes the document: fails when containers or strings are still open, which
// is how a truncated body is caught rather than salvaged.
bool transit_json_finish(transit_json_parser_t *parser);

// Parses an ISO-8601 timestamp with an explicit offset, e.g.
// "2026-09-12T13:56:48+08:00", to a Unix epoch. Does not use mktime(): that
// interprets a struct tm in the device's local zone, which would shift every
// ETA by the timezone offset the moment the two disagree. Returns 0 on failure.
int32_t transit_parse_iso8601(const char *text);

#ifdef __cplusplus
}
#endif
