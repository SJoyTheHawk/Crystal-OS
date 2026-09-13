/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "transit_json.h"
#include "transit_service.h"

#ifdef __cplusplus
extern "C" {
#endif

// A kept-alive client, reused across a batch of requests to one host. Resolving
// 60-200 stop names is 60-200 requests, and a TLS session per stop costs
// 600-900 ms each against roughly 100 ms on a reused connection.
typedef struct transit_client transit_client_t;

transit_client_t *transit_client_open(const char *url);
transit_client_t *transit_client_open_timeout(const char *url, int timeout_ms);
void transit_client_close(transit_client_t *client);

// Cancellation is polled between reads. Returning true abandons the request.
typedef bool (*transit_cancel_fn)(void *user_data);

// Sees every body byte as it streams past, before the scanner. Used to hash a
// body whose server offers no validator.
typedef void (*transit_bytes_fn)(const char *data, size_t length, void *user_data);

typedef struct {
    const char       *if_none_match;   // sends If-None-Match when non-NULL
    char             *etag_out;        // receives the response ETag
    size_t            etag_size;
    transit_bytes_fn  on_bytes;
    void             *bytes_data;
    transit_cancel_fn should_cancel;
    void             *cancel_data;
    bool              not_modified;    // set true when the server answered 304
    int               timeout_ms;      // 0 uses the default
} transit_http_options_t;

// As transit_http_get_json(), with conditional-GET and streaming hooks. A 304
// returns TRANSIT_STATUS_OK with options->not_modified set and nothing parsed.
transit_status_t transit_http_get_json_ex(transit_client_t *client, const char *url,
                                          transit_json_parser_t *parser,
                                          transit_http_options_t *options);

// Streams one GET through the scanner. `client` may be NULL, in which case a
// throwaway client is used for this request only.
//
// HTTPS only, with the ESP-IDF certificate bundle -- the same trust store
// Weather uses. There is no per-endpoint PEM: the feeds sit behind Azure Front
// Door and CloudFront, whose leaf chains rotate, so pinning a single CA is both
// unnecessary and a future outage.
transit_status_t transit_http_get_json(transit_client_t *client, const char *url,
                                       transit_json_parser_t *parser,
                                       transit_cancel_fn should_cancel, void *cancel_data);

#ifdef __cplusplus
}
#endif
