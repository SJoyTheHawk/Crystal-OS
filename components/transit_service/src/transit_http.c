/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */

#include "transit_http.h"

#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "transit_http";

// One read window, reused for every request. The scanner is incremental, so the
// body never needs to be resident: a 3 KB route-stop list and a 112 KB route
// table both pass through this same buffer.
#define TRANSIT_HTTP_WINDOW 1024

struct transit_client {
    esp_http_client_handle_t handle;
    // True when the previous response's body was read to completion. Only then is
    // the socket at a message boundary and safe to reuse; a body abandoned
    // part-way leaves bytes the next response would read as its own.
    bool                     reusable;
    char                     buffer[TRANSIT_HTTP_WINDOW];
};

transit_client_t *transit_client_open_timeout(const char *url, int timeout_ms)
{
    if (url == NULL) return NULL;
    transit_client_t *client = calloc(1, sizeof(*client));
    if (client == NULL) return NULL;

    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = timeout_ms > 0 ? timeout_ms : 10000,
        // TCP SO_KEEPALIVE, which is not HTTP connection reuse. Reuse is decided
        // by whether esp_http_client_close() is called -- see the note there.
        .keep_alive_enable = true,
        // Lets the handle resume its TLS session when it does have to reconnect,
        // e.g. after the server closes an idle socket. One round trip instead of
        // a full handshake. Needs CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS.
        .save_client_session = true,
    };
    client->handle = esp_http_client_init(&config);
    if (client->handle == NULL) {
        free(client);
        return NULL;
    }
    return client;
}

transit_client_t *transit_client_open(const char *url)
{
    return transit_client_open_timeout(url, 0);
}

void transit_client_close(transit_client_t *client)
{
    if (client == NULL) return;
    if (client->handle != NULL) {
        // Requests now leave the connection open for the next one, so the last of
        // them has a live socket to shut down here. cleanup() frees the handle
        // either way; closing first sends the TLS close-notify rather than
        // dropping the peer mid-session.
        if (client->reusable) esp_http_client_close(client->handle);
        esp_http_client_cleanup(client->handle);
    }
    free(client);
}

transit_status_t transit_http_get_json(transit_client_t *client, const char *url,
                                       transit_json_parser_t *parser,
                                       transit_cancel_fn should_cancel, void *cancel_data)
{
    transit_http_options_t options = {
        .should_cancel = should_cancel,
        .cancel_data = cancel_data,
    };
    return transit_http_get_json_ex(client, url, parser, &options);
}

transit_status_t transit_http_get_json_ex(transit_client_t *client, const char *url,
                                         transit_json_parser_t *parser,
                                         transit_http_options_t *options)
{
    static transit_http_options_t s_defaults;
    if (options == NULL) { s_defaults = (transit_http_options_t){0}; options = &s_defaults; }
    if (url == NULL || parser == NULL) return TRANSIT_STATUS_PARSE_ERROR;

    const transit_cancel_fn should_cancel = options->should_cancel;
    void *cancel_data = options->cancel_data;
    options->not_modified = false;

    transit_client_t *owned = NULL;
    if (client == NULL) {
        owned = transit_client_open_timeout(url, options->timeout_ms);
        if (owned == NULL) {
            ESP_LOGW(TAG, "client allocation failed");
            return TRANSIT_STATUS_OFFLINE;
        }
        client = owned;
    } else if (esp_http_client_set_url(client->handle, url) != ESP_OK) {
        ESP_LOGW(TAG, "set_url failed for %s", url);
        return TRANSIT_STATUS_PARSE_ERROR;
    }

    transit_status_t status = TRANSIT_STATUS_OK;
    // Whether the socket is at a message boundary at the end. Only then can the
    // connection be handed to the next request.
    bool body_complete = false;

    if (options->if_none_match != NULL && options->if_none_match[0] != '\0') {
        // The steady state for the KMB route table: a 304 with a zero-length body
        // instead of 349 KB.
        (void)esp_http_client_set_header(client->handle, "If-None-Match",
                                         options->if_none_match);
    }

    // open/fetch_headers/read/close, never perform(). perform() drains the body
    // internally to satisfy content_length and does not cache it, so a following
    // read_response() returns 0 bytes -- CODE_GUIDE.md
    // §"perform() does not leave you a body". It bites harder here, where bodies
    // are streamed rather than buffered.
    const esp_err_t opened = esp_http_client_open(client->handle, 0);
    if (opened != ESP_OK) {
        // No route to host, DNS failure, or a TLS handshake that did not
        // complete. All of them mean the same thing to the user, and none of
        // them is a bad response.
        //
        // The internal figure is logged with it because a TLS handshake that
        // fails for memory looks identical from here to one that fails for the
        // network: mbedtls reports -0x008D (SSL_ALLOC_FAILED) and esp_http_client
        // flattens it to ESP_ERR_HTTP_CONNECT. Largest-block matters more than the
        // total -- the record buffers are single allocations.
        ESP_LOGW(TAG, "open failed for %s: %s (internal heap %u free, %u largest)",
                 url, esp_err_to_name(opened),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                            MALLOC_CAP_8BIT));
        // open() failing leaves nothing to reuse, and the flag must not keep the
        // value the previous request left: transit_client_close() reads it.
        client->reusable = false;
        status = TRANSIT_STATUS_OFFLINE;
        goto done;
    }

    const int64_t declared = esp_http_client_fetch_headers(client->handle);
    const int code = esp_http_client_get_status_code(client->handle);

    if (options->etag_out != NULL && options->etag_size > 0) {
        char *value = NULL;
        options->etag_out[0] = '\0';
        if (esp_http_client_get_header(client->handle, "ETag", &value) == ESP_OK &&
            value != NULL) {
            strlcpy(options->etag_out, value, options->etag_size);
        }
    }

    if (code == 304) {
        options->not_modified = true;
        body_complete = true;    // a 304 has no body, so the socket is at a boundary
        goto close_connection;   // nothing to read, nothing to parse
    }
    if (code != 200) {
        // 422 is what KMB answers for a malformed direction, 404 for an unknown
        // route. Both are the server refusing, not a transport fault.
        ESP_LOGW(TAG, "http %d for %s", code, url);
        status = TRANSIT_STATUS_HTTP_ERROR;
        goto close_connection;
    }

    for (;;) {
        if (should_cancel != NULL && should_cancel(cancel_data)) {
            status = TRANSIT_STATUS_CANCELLED;
            goto close_connection;
        }
        const int read = esp_http_client_read(client->handle, client->buffer,
                                              sizeof(client->buffer));
        if (read < 0) {
            ESP_LOGW(TAG, "read error for %s", url);
            status = TRANSIT_STATUS_OFFLINE;
            goto close_connection;
        }
        if (read == 0) { body_complete = true; break; }

        if (options->on_bytes != NULL) {
            options->on_bytes(client->buffer, (size_t)read, options->bytes_data);
        }

        if (!transit_json_feed(parser, client->buffer, (size_t)read)) {
            // A consumer that had enough leaves bytes on the socket, so this exit
            // is deliberately not a boundary: body_complete stays false and the
            // connection is dropped rather than handed on.
            if (transit_json_stopped(parser)) break;
            ESP_LOGW(TAG, "malformed json from %s (declared %lld)", url,
                     (long long)declared);
            status = TRANSIT_STATUS_PARSE_ERROR;
            goto close_connection;
        }
    }

    if (!transit_json_finish(parser)) {
        // Unbalanced containers mean a truncated body. The cache stays untouched
        // rather than taking partial records.
        ESP_LOGW(TAG, "truncated json from %s", url);
        status = TRANSIT_STATUS_PARSE_ERROR;
    }

close_connection:
    // esp_http_client_close() calls esp_transport_close(), which drops the TLS
    // session outright. Calling it after every request is what made a "kept-alive"
    // client pay a fresh ~700 ms handshake per stop name: 50 stops cost 35 s of
    // handshake for ~5 s of work. The handle is left open when the body was fully
    // consumed, so the next esp_http_client_open() finds state == CONNECTED and
    // reuses the socket.
    //
    // It is still closed on any path that did not reach a message boundary: a
    // cancelled read, a parse that stopped early, or a transport error. Reusing
    // one of those would put the tail of one body at the head of the next.
    if (body_complete) {
        client->reusable = true;
    } else {
        client->reusable = false;
        esp_http_client_close(client->handle);
    }
done:
    if (owned != NULL) transit_client_close(owned);
    return status;
}
