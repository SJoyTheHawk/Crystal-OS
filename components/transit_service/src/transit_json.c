/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */

#include "transit_json.h"

#include <stdlib.h>
#include <string.h>

static void reset_token(transit_json_parser_t *p)
{
    p->token_len = 0;
    p->token[0] = '\0';
    p->overflow = false;
}

static void push_char(transit_json_parser_t *p, char c)
{
    if (p->token_len + 1 < sizeof(p->token)) {
        p->token[p->token_len++] = c;
        p->token[p->token_len] = '\0';
    } else {
        // Third-party data on a device with no MMU: a 3,000-character dest_en
        // truncates, it does not grow a buffer.
        p->overflow = true;
    }
}

static const char *current_key(const transit_json_parser_t *p)
{
    if (p->depth == 0 || p->depth > TRANSIT_JSON_DEPTH_MAX) return "";
    return p->key[p->depth - 1];
}

static bool innermost_is_array(const transit_json_parser_t *p)
{
    return p->depth > 0 && p->depth <= TRANSIT_JSON_DEPTH_MAX && p->is_array[p->depth - 1];
}

static bool emit(transit_json_parser_t *p, transit_json_value_type_t type)
{
    if (p->on_member == NULL) return true;
    transit_json_member_t member = {
        .depth = p->depth,
        .in_array = innermost_is_array(p),
        .key = current_key(p),
        .value = p->token,
        .truncated = p->overflow,
        .type = type,
    };
    return p->on_member(&member, p->user_data);
}

static void clear_key(transit_json_parser_t *p)
{
    if (p->depth > 0 && p->depth <= TRANSIT_JSON_DEPTH_MAX) p->key[p->depth - 1][0] = '\0';
    p->have_key = false;
}

void transit_json_init(transit_json_parser_t *parser, transit_json_cb_t on_member,
                       transit_json_object_end_cb_t on_object_end, void *user_data)
{
    if (parser == NULL) return;
    memset(parser, 0, sizeof(*parser));
    parser->on_member = on_member;
    parser->on_object_end = on_object_end;
    parser->user_data = user_data;
}

// Closes a bare number or literal. Called when the terminator is seen, which may
// be in a later chunk than the token started in.
static bool close_scalar(transit_json_parser_t *p)
{
    if (!p->in_scalar) return true;
    p->in_scalar = false;
    const bool numeric = p->token_len > 0 &&
                         (p->token[0] == '-' || (p->token[0] >= '0' && p->token[0] <= '9'));
    const bool ok = emit(p, numeric ? TRANSIT_JSON_NUMBER : TRANSIT_JSON_LITERAL);
    clear_key(p);
    reset_token(p);
    return ok;
}

bool transit_json_feed(transit_json_parser_t *parser, const char *data, size_t length)
{
    if (parser == NULL || data == NULL) return false;
    if (parser->failed || parser->stopped) return false;

    for (size_t i = 0; i < length; ++i) {
        const char c = data[i];

        if (parser->in_string) {
            if (parser->escaped) {
                parser->escaped = false;
                switch (c) {
                case 'n': push_char(parser, '\n'); break;
                case 't': push_char(parser, '\t'); break;
                case 'r': push_char(parser, '\r'); break;
                case 'b': push_char(parser, '\b'); break;
                case 'f': push_char(parser, '\f'); break;
                // \uXXXX is kept as its literal characters rather than decoded:
                // every field this service reads is ASCII from the operators,
                // and a partial decoder would be a second bug surface.
                default:  push_char(parser, c); break;
                }
                continue;
            }
            if (c == '\\') { parser->escaped = true; continue; }
            if (c == '"') {
                parser->in_string = false;
                if (parser->is_key) {
                    if (parser->depth > 0 && parser->depth <= TRANSIT_JSON_DEPTH_MAX) {
                        strlcpy(parser->key[parser->depth - 1], parser->token,
                                TRANSIT_JSON_KEY_MAX);
                    }
                    parser->have_key = true;
                    parser->is_key = false;
                    reset_token(parser);
                } else {
                    if (!emit(parser, TRANSIT_JSON_STRING)) { parser->stopped = true; return false; }
                    clear_key(parser);
                    reset_token(parser);
                }
                continue;
            }
            push_char(parser, c);
            continue;
        }

        switch (c) {
        case '"':
            if (!close_scalar(parser)) { parser->stopped = true; return false; }
            parser->in_string = true;
            // Inside an object, a string that starts a member is its key. Inside
            // an array, or right after a colon, it is a value.
            parser->is_key = !innermost_is_array(parser) && !parser->have_key;
            reset_token(parser);
            break;

        case '{':
        case '[':
            if (!close_scalar(parser)) { parser->stopped = true; return false; }
            if (parser->depth >= TRANSIT_JSON_DEPTH_MAX) { parser->failed = true; return false; }
            parser->depth++;
            parser->is_array[parser->depth - 1] = (c == '[');
            parser->key[parser->depth - 1][0] = '\0';
            parser->have_key = false;
            break;

        case '}':
        case ']':
            if (!close_scalar(parser)) { parser->stopped = true; return false; }
            if (parser->depth == 0) { parser->failed = true; return false; }
            if (parser->is_array[parser->depth - 1] != (c == ']')) {
                parser->failed = true;   // mismatched close
                return false;
            }
            if (c == '}' && parser->on_object_end != NULL) {
                // The enclosing container, one level out from the object closing
                // here. An element of "data":[...] reports true; the envelope
                // object itself reports false.
                const bool in_array = parser->depth >= 2 && parser->is_array[parser->depth - 2];
                if (!parser->on_object_end(parser->depth, in_array, parser->user_data)) {
                    parser->stopped = true;
                    return false;
                }
            }
            parser->depth--;
            parser->have_key = false;
            break;

        case ':':
        case ',':
            if (!close_scalar(parser)) { parser->stopped = true; return false; }
            if (c == ',') clear_key(parser);
            break;

        case ' ': case '\t': case '\n': case '\r':
            if (!close_scalar(parser)) { parser->stopped = true; return false; }
            break;

        default:
            if (!parser->in_scalar) {
                parser->in_scalar = true;
                reset_token(parser);
            }
            push_char(parser, c);
            break;
        }
    }
    return true;
}

bool transit_json_failed(const transit_json_parser_t *parser)
{
    return parser == NULL || parser->failed;
}

bool transit_json_stopped(const transit_json_parser_t *parser)
{
    return parser != NULL && parser->stopped;
}

bool transit_json_finish(transit_json_parser_t *parser)
{
    if (parser == NULL || parser->failed) return false;
    if (parser->stopped) return true;   // deliberate early exit, not a failure
    if (!close_scalar(parser)) return true;
    // An unbalanced document is a truncated one. Fail the whole request rather
    // than salvage partial records.
    if (parser->in_string || parser->depth != 0) {
        parser->failed = true;
        return false;
    }
    return true;
}

static int digits(const char *text, size_t count)
{
    int value = 0;
    for (size_t i = 0; i < count; ++i) {
        if (text[i] < '0' || text[i] > '9') return -1;
        value = value * 10 + (text[i] - '0');
    }
    return value;
}

// Days since 1970-01-01 for a civil date. Howard Hinnant's days_from_civil.
static int64_t days_from_civil(int year, int month, int day)
{
    year -= month <= 2;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy = (unsigned)((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

int32_t transit_parse_iso8601(const char *text)
{
    if (text == NULL || strlen(text) < 19) return 0;
    if (text[4] != '-' || text[7] != '-' || text[13] != ':' || text[16] != ':') return 0;
    if (text[10] != 'T' && text[10] != ' ') return 0;

    const int year = digits(text, 4);
    const int month = digits(text + 5, 2);
    const int day = digits(text + 8, 2);
    const int hour = digits(text + 11, 2);
    const int minute = digits(text + 14, 2);
    const int second = digits(text + 17, 2);
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31) return 0;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) return 0;

    int64_t epoch = days_from_civil(year, month, day) * 86400 +
                    hour * 3600 + minute * 60 + second;

    // Offset. The feeds send +08:00; 'Z' and a missing offset are treated as UTC.
    const char *rest = text + 19;
    if (*rest == '.') {                    // fractional seconds, discarded
        ++rest;
        while (*rest >= '0' && *rest <= '9') ++rest;
    }
    if (*rest == '+' || *rest == '-') {
        const int sign = (*rest == '+') ? 1 : -1;
        ++rest;
        if (strlen(rest) < 2) return 0;
        const int offset_hour = digits(rest, 2);
        if (offset_hour < 0) return 0;
        int offset_minute = 0;
        const char *minutes = (rest[2] == ':') ? rest + 3 : rest + 2;
        if (strlen(minutes) >= 2) {
            const int parsed = digits(minutes, 2);
            if (parsed >= 0) offset_minute = parsed;
        }
        epoch -= sign * (offset_hour * 3600 + offset_minute * 60);
    }

    if (epoch < 0 || epoch > INT32_MAX) return 0;
    return (int32_t)epoch;
}
