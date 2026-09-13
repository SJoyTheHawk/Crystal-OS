/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */

#include "transit_routes.h"

#include <string.h>

#include "transit_index.h"

// Every lookup here depends on the active name table being sorted ascending by
// memcmp over the padded name. The generator asserts it; verify_sorted() checks
// it once at first use so a bad hand-edit fails loudly instead of silently
// greying out real routes.
static bool s_sorted_checked;
static bool s_sorted_ok;

// The active tables. These point at the compiled baseline until
// transit_index_install() swaps in a refreshed index.
static const transit_route_name_t    *s_names = transit_route_index;
static uint16_t                       s_name_count;
static const transit_route_variant_t *s_variants = transit_route_variants;
static uint16_t                       s_variant_count;
static bool                           s_loaded;

void transit_index_install(const transit_route_name_t *names, uint16_t name_count,
                           const transit_route_variant_t *variants, uint16_t variant_count)
{
    if (names == NULL || name_count == 0) return;
    s_names = names;
    s_name_count = name_count;
    s_variants = variants;
    s_variant_count = variants != NULL ? variant_count : 0;
    s_sorted_checked = false;   // re-verify the table that just arrived
}

// Resolves the table pointers on first use. The persisted index is read here
// rather than at boot: a device whose owner never opens the bus app pays no
// SPIFFS read for it.
static void ensure_loaded(void)
{
    if (s_loaded) return;
    s_loaded = true;
    s_name_count = transit_route_index_count;
    s_variant_count = transit_route_variant_count;
    transit_index_load();
}

static void pad_name(const char *text, size_t length, char out[TRANSIT_ROUTE_NAME_LEN])
{
    memset(out, ' ', TRANSIT_ROUTE_NAME_LEN);
    if (text == NULL) return;
    if (length > TRANSIT_ROUTE_NAME_LEN) length = TRANSIT_ROUTE_NAME_LEN;
    memcpy(out, text, length);
}

static bool verify_sorted(void)
{
    ensure_loaded();
    if (s_sorted_checked) return s_sorted_ok;
    s_sorted_checked = true;
    s_sorted_ok = true;
    for (uint16_t i = 1; i < s_name_count; ++i) {
        if (memcmp(s_names[i - 1].name, s_names[i].name, TRANSIT_ROUTE_NAME_LEN) >= 0) {
            s_sorted_ok = false;
            break;
        }
    }
    return s_sorted_ok;
}

// First index whose first `length` bytes are >= the padded prefix. The prefix is
// compared over `length` bytes only, so this brackets the whole prefix range.
static uint16_t lower_bound(const char padded[TRANSIT_ROUTE_NAME_LEN], size_t length)
{
    uint16_t low = 0;
    uint16_t high = s_name_count;
    while (low < high) {
        const uint16_t mid = (uint16_t)(low + (high - low) / 2);
        if (memcmp(s_names[mid].name, padded, length) < 0) low = (uint16_t)(mid + 1);
        else high = mid;
    }
    return low;
}

const char *transit_route_charset(void)
{
    return transit_charset;
}

uint32_t transit_route_next_mask(const char *prefix, size_t length)
{
    // A name is never longer than four characters, so nothing can follow a
    // full-length prefix. The keypad must not be the thing that enforces that.
    if (prefix == NULL || length >= TRANSIT_ROUTE_NAME_LEN) return 0;
    if (!verify_sorted()) return 0;

    char padded[TRANSIT_ROUTE_NAME_LEN];
    pad_name(prefix, length, padded);

    uint32_t mask = 0;
    for (uint16_t i = lower_bound(padded, length); i < s_name_count; ++i) {
        if (memcmp(s_names[i].name, padded, length) != 0) break;
        const char next = s_names[i].name[length];
        if (next == ' ') continue;   // this record ends here
        const char *found = strchr(transit_charset, next);
        if (found != NULL) mask |= 1u << (unsigned)(found - transit_charset);
    }
    return mask;
}

uint8_t transit_route_is_complete(const char *name, size_t length)
{
    if (name == NULL || length == 0 || length > TRANSIT_ROUTE_NAME_LEN) return 0;
    if (!verify_sorted()) return 0;

    char padded[TRANSIT_ROUTE_NAME_LEN];
    pad_name(name, length, padded);

    const uint16_t at = lower_bound(padded, TRANSIT_ROUTE_NAME_LEN);
    if (at >= s_name_count) return 0;
    if (memcmp(s_names[at].name, padded, TRANSIT_ROUTE_NAME_LEN) != 0) return 0;
    return s_names[at].ops;
}

size_t transit_route_kmb_variants(const char *name, size_t length,
                                  transit_route_variant_t *out, size_t capacity)
{
    if (name == NULL || out == NULL || capacity == 0) return 0;
    if (length == 0 || length > TRANSIT_ROUTE_NAME_LEN) return 0;
    if (!verify_sorted()) return 0;

    char padded[TRANSIT_ROUTE_NAME_LEN];
    pad_name(name, length, padded);

    const uint16_t at = lower_bound(padded, TRANSIT_ROUTE_NAME_LEN);
    if (at >= s_name_count) return 0;
    if (memcmp(s_names[at].name, padded, TRANSIT_ROUTE_NAME_LEN) != 0) return 0;
    if ((s_names[at].ops & TRANSIT_OP_MASK_KMB) == 0) return 0;

    // The variant table is sorted by (name_index, bound, service_type), so the
    // matching run is contiguous.
    size_t written = 0;
    for (uint16_t i = 0; i < s_variant_count; ++i) {
        if (s_variants[i].name_index < at) continue;
        if (s_variants[i].name_index > at) break;
        if (written >= capacity) break;
        out[written++] = s_variants[i];
    }
    return written;
}
