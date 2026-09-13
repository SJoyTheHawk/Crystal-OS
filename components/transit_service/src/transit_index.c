/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */

#include "transit_index.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
// mbedTLS 4 moved the hash primitives behind PSA crypto, so mbedtls/sha256.h no
// longer exists in IDF 6.1. psa_hash_* is the streaming equivalent and is what
// esp-tls itself uses.
#include "psa/crypto.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "transit_http.h"
#include "transit_json.h"
#include "transit_routes.h"

static const char *TAG = "transit_index";

#define KMB_ROUTE_TABLE "https://data.etabus.gov.hk/v1/transport/kmb/route/"
#define CTB_ROUTE_TABLE "https://rt.data.gov.hk/v2/transport/citybus/route/CTB"

// Its own NVS namespace, not CrystalState: CrystalState belongs to the app, and
// its 2,048-byte value cap is beside the point for keys this small.
#define NVS_NAMESPACE "transit"
#define KEY_ETAG      "tr.etag"
#define KEY_CTB_HASH  "tr.ctbhash"
#define KEY_CHECKED   "tr.checked"

#define KMB_INTERVAL_S (24 * 60 * 60)        // ETag conditional GET is nearly free
#define CTB_INTERVAL_S (7 * 24 * 60 * 60)    // 112 KB every time, so weekly
#define MIN_PLAUSIBLE_EPOCH 1577836800       // 2020-01-01; below this the clock is unset

// Working caps for a rebuild. Today: 1,048 names and 1,599 KMB variants. The
// headroom absorbs years of growth and still fits the 64 KB PSRAM gate --
// 2048*5 + 3072*4 = 22,528 bytes.
#define MAX_NAMES    2048
#define MAX_VARIANTS 3072

#define INDEX_MAGIC   0x54524958u   // "TRIX"
#define INDEX_VERSION 1

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t name_count;
    uint16_t variant_count;
    uint16_t reserved;
    uint32_t checksum;      // CRC-32 over the records that follow
} index_header_t;

// The installed index, held for the life of the process once loaded. Freeing it
// would strand the pointers transit_index_install() handed to the lookups.
static transit_route_name_t    *s_names;
static transit_route_variant_t *s_variants;
static int32_t                  s_checked_at;
static bool                     s_nvs_read;

// ------------------------------------------------------------------- NVS access

static nvs_handle_t open_nvs(nvs_open_mode_t mode)
{
    nvs_handle_t handle = 0;
    if (nvs_open(NVS_NAMESPACE, mode, &handle) != ESP_OK) return 0;
    return handle;
}

static void read_checked_at(void)
{
    if (s_nvs_read) return;
    s_nvs_read = true;
    nvs_handle_t handle = open_nvs(NVS_READONLY);
    if (handle == 0) return;
    int32_t value = 0;
    if (nvs_get_i32(handle, KEY_CHECKED, &value) == ESP_OK) s_checked_at = value;
    nvs_close(handle);
}

int32_t transit_index_checked_at(void)
{
    read_checked_at();
    return s_checked_at;
}

static void store_checked_at(int32_t when)
{
    nvs_handle_t handle = open_nvs(NVS_READWRITE);
    if (handle == 0) return;
    if (nvs_set_i32(handle, KEY_CHECKED, when) == ESP_OK) (void)nvs_commit(handle);
    nvs_close(handle);
    s_checked_at = when;
    s_nvs_read = true;
}

// --------------------------------------------------------------------- checksum

static uint32_t crc32(const void *data, size_t length, uint32_t seed)
{
    const uint8_t *bytes = data;
    uint32_t crc = ~seed;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
        }
    }
    return ~crc;
}

// ------------------------------------------------------------------ persistence

void transit_index_load(void)
{
    if (s_names != NULL) return;   // already installed this boot

    FILE *file = fopen(TRANSIT_INDEX_PATH, "rb");
    if (file == NULL) return;      // no refreshed index yet; baseline stands

    index_header_t header = {0};
    bool ok = fread(&header, sizeof(header), 1, file) == 1 &&
              header.magic == INDEX_MAGIC && header.version == INDEX_VERSION &&
              header.name_count > 0 && header.name_count <= MAX_NAMES &&
              header.variant_count <= MAX_VARIANTS;

    transit_route_name_t *names = NULL;
    transit_route_variant_t *variants = NULL;
    if (ok) {
        names = heap_caps_calloc(header.name_count, sizeof(*names), MALLOC_CAP_SPIRAM);
        variants = header.variant_count > 0
                 ? heap_caps_calloc(header.variant_count, sizeof(*variants), MALLOC_CAP_SPIRAM)
                 : NULL;
        ok = names != NULL && (header.variant_count == 0 || variants != NULL);
    }
    if (ok) {
        ok = fread(names, sizeof(*names), header.name_count, file) == header.name_count;
    }
    if (ok && header.variant_count > 0) {
        ok = fread(variants, sizeof(*variants), header.variant_count, file) == header.variant_count;
    }
    fclose(file);

    if (ok) {
        uint32_t checksum = crc32(names, (size_t)header.name_count * sizeof(*names), 0);
        if (header.variant_count > 0) {
            checksum = crc32(variants, (size_t)header.variant_count * sizeof(*variants), checksum);
        }
        ok = checksum == header.checksum;
    }
    if (ok) {
        // Sortedness is the invariant every lookup depends on, so it is checked
        // here rather than trusted from a file that may have been written by an
        // older build.
        for (uint16_t i = 1; i < header.name_count && ok; ++i) {
            if (memcmp(names[i - 1].name, names[i].name, TRANSIT_ROUTE_NAME_LEN) >= 0) ok = false;
        }
    }

    if (!ok) {
        // A corrupt index would grey out real routes with no way for the user to
        // tell why. Delete it and fall back to the baseline.
        ESP_LOGW(TAG, "discarding invalid %s", TRANSIT_INDEX_PATH);
        free(names);
        free(variants);
        (void)unlink(TRANSIT_INDEX_PATH);
        return;
    }

    s_names = names;
    s_variants = variants;
    transit_index_install(names, header.name_count, variants, header.variant_count);
    ESP_LOGI(TAG, "loaded refreshed index: %u names, %u variants",
             (unsigned)header.name_count, (unsigned)header.variant_count);
}

static bool write_index_file(const transit_route_name_t *names, uint16_t name_count,
                             const transit_route_variant_t *variants, uint16_t variant_count)
{
    (void)mkdir("/spiffs/transit", 0777);

    // Write through a .tmp and rename, so a power loss mid-write leaves the
    // previous index intact.
    const char *temp = TRANSIT_INDEX_PATH ".tmp";
    FILE *file = fopen(temp, "wb");
    if (file == NULL) {
        ESP_LOGW(TAG, "cannot open %s", temp);
        return false;
    }

    index_header_t header = {
        .magic = INDEX_MAGIC,
        .version = INDEX_VERSION,
        .name_count = name_count,
        .variant_count = variant_count,
        .checksum = crc32(names, (size_t)name_count * sizeof(*names), 0),
    };
    if (variant_count > 0) {
        header.checksum = crc32(variants, (size_t)variant_count * sizeof(*variants),
                                header.checksum);
    }

    bool ok = fwrite(&header, sizeof(header), 1, file) == 1 &&
              fwrite(names, sizeof(*names), name_count, file) == name_count;
    if (ok && variant_count > 0) {
        ok = fwrite(variants, sizeof(*variants), variant_count, file) == variant_count;
    }
    if (fclose(file) != 0) ok = false;

    if (!ok) {
        ESP_LOGW(TAG, "write failed; keeping previous index");
        (void)unlink(temp);
        return false;
    }
    (void)unlink(TRANSIT_INDEX_PATH);
    if (rename(temp, TRANSIT_INDEX_PATH) != 0) {
        ESP_LOGW(TAG, "rename failed; keeping previous index");
        (void)unlink(temp);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- table builder

// A variant as scanned, before names are sorted. Holding the raw name lets the
// name table be sorted independently and the indices remapped afterwards.
typedef struct {
    char    route[TRANSIT_ROUTE_NAME_LEN];
    char    bound;
    uint8_t service_type;
} scanned_variant_t;

typedef struct {
    transit_route_name_t *names;
    uint16_t              name_count;
    scanned_variant_t    *variants;
    uint16_t              variant_count;
    uint8_t               op_mask;      // which operator this stream belongs to
    bool                  overflow;

    // Current record.
    char    route[TRANSIT_ROUTE_NAME_LEN + 1];
    char    bound;
    uint8_t service_type;
} builder_t;

static bool name_is_valid(const char *name, size_t length)
{
    if (length == 0 || length > TRANSIT_ROUTE_NAME_LEN) return false;
    for (size_t i = 0; i < length; ++i) {
        if (strchr(transit_charset, name[i]) == NULL) return false;
    }
    return true;
}

// Linear insert into an unsorted array, deduplicating by name. 1,000 names at
// ~500 comparisons each is trivial next to the transfer that produced them.
static void add_name(builder_t *builder, const char *name, size_t length)
{
    char padded[TRANSIT_ROUTE_NAME_LEN];
    memset(padded, ' ', sizeof(padded));
    memcpy(padded, name, length);

    for (uint16_t i = 0; i < builder->name_count; ++i) {
        if (memcmp(builder->names[i].name, padded, TRANSIT_ROUTE_NAME_LEN) == 0) {
            builder->names[i].ops |= builder->op_mask;
            return;
        }
    }
    if (builder->name_count >= MAX_NAMES) { builder->overflow = true; return; }
    memcpy(builder->names[builder->name_count].name, padded, TRANSIT_ROUTE_NAME_LEN);
    builder->names[builder->name_count].ops = builder->op_mask;
    builder->name_count++;
}

static bool builder_member(const transit_json_member_t *member, void *user_data)
{
    builder_t *builder = user_data;
    if (member->truncated) return true;   // not a route name we can use
    if (strcmp(member->key, "route") == 0) {
        strlcpy(builder->route, member->value, sizeof(builder->route));
    } else if (strcmp(member->key, "bound") == 0) {
        builder->bound = member->value[0];
    } else if (strcmp(member->key, "service_type") == 0) {
        builder->service_type = (uint8_t)atoi(member->value);
    }
    return true;
}

static bool builder_object_end(uint8_t depth, bool in_array, void *user_data)
{
    (void)depth;
    builder_t *builder = user_data;
    // Both route tables are a "data" array of flat objects.
    if (!in_array) return true;

    const size_t length = strlen(builder->route);
    if (name_is_valid(builder->route, length)) {
        add_name(builder, builder->route, length);
        // Only KMB carries variant identity; CTB has no direction dimension in
        // its table and its endpoints take the direction words directly.
        if (builder->op_mask == TRANSIT_OP_MASK_KMB &&
            (builder->bound == 'O' || builder->bound == 'I') &&
            builder->service_type > 0) {
            if (builder->variant_count < MAX_VARIANTS) {
                scanned_variant_t *variant = &builder->variants[builder->variant_count++];
                memset(variant->route, ' ', TRANSIT_ROUTE_NAME_LEN);
                memcpy(variant->route, builder->route, length);
                variant->bound = builder->bound;
                variant->service_type = builder->service_type;
            } else {
                builder->overflow = true;
            }
        }
    }

    builder->route[0] = '\0';
    builder->bound = '\0';
    builder->service_type = 0;
    return true;
}

static int compare_names(const void *lhs, const void *rhs)
{
    return memcmp(((const transit_route_name_t *)lhs)->name,
                  ((const transit_route_name_t *)rhs)->name, TRANSIT_ROUTE_NAME_LEN);
}

static int compare_variants(const void *lhs, const void *rhs)
{
    const transit_route_variant_t *a = lhs;
    const transit_route_variant_t *b = rhs;
    if (a->name_index != b->name_index) return a->name_index < b->name_index ? -1 : 1;
    if (a->bound != b->bound) return a->bound < b->bound ? -1 : 1;
    if (a->service_type != b->service_type) return a->service_type < b->service_type ? -1 : 1;
    return 0;
}

// ------------------------------------------------------------------ the refresh

typedef struct {
    psa_hash_operation_t operation;
    bool                 usable;
} hash_ctx_t;

static void hash_bytes(const char *data, size_t length, void *user_data)
{
    hash_ctx_t *ctx = user_data;
    if (!ctx->usable) return;
    if (psa_hash_update(&ctx->operation, (const uint8_t *)data, length) != PSA_SUCCESS) {
        ctx->usable = false;
    }
}

// Rebuilds the whole index from both feeds and installs it. Called only when a
// validator says something changed, which is a few times a year.
// `kmb_etag_in` is ignored when `force` is set, because a 304 would leave the
// rebuild with no KMB names at all. force is how the weekly CTB check gets a
// complete picture on a day KMB has not changed.
static bool rebuild(const char *kmb_etag_in, bool force,
                    char *kmb_etag_out, size_t etag_size,
                    unsigned char ctb_hash_out[32], bool *kmb_changed)
{
    // One allocation pair for the rebuild, freed before returning. 22.5 KB of
    // PSRAM against the 64 KB gate.
    transit_route_name_t *names = heap_caps_calloc(MAX_NAMES, sizeof(*names), MALLOC_CAP_SPIRAM);
    scanned_variant_t *scanned = heap_caps_calloc(MAX_VARIANTS, sizeof(*scanned), MALLOC_CAP_SPIRAM);
    transit_route_variant_t *variants =
        heap_caps_calloc(MAX_VARIANTS, sizeof(*variants), MALLOC_CAP_SPIRAM);
    if (names == NULL || scanned == NULL || variants == NULL) {
        ESP_LOGW(TAG, "rebuild allocation failed");
        free(names); free(scanned); free(variants);
        return false;
    }

    builder_t builder = {.names = names, .variants = scanned};
    bool ok = true;

    // KMB. The ETag is content-derived and If-None-Match returns 304 with a
    // zero-length body, so this is the cheap side. generated_timestamp is NOT a
    // change detector: it tracks the max-age=300 cache window and advances every
    // window whether or not a route changed.
    builder.op_mask = TRANSIT_OP_MASK_KMB;
    transit_json_parser_t parser;
    transit_json_init(&parser, builder_member, builder_object_end, &builder);
    transit_http_options_t kmb_options = {
        .if_none_match = force ? NULL : kmb_etag_in,
        .etag_out = kmb_etag_out,
        .etag_size = etag_size,
        .timeout_ms = 30000,   // 349 KB over a slow link
    };
    transit_status_t status = transit_http_get_json_ex(NULL, KMB_ROUTE_TABLE, &parser,
                                                      &kmb_options);
    if (status != TRANSIT_STATUS_OK) ok = false;
    *kmb_changed = ok && !kmb_options.not_modified;

    if (ok && kmb_options.not_modified) {
        // Nothing changed on the expensive feed. Do not rebuild from a partial
        // picture: the KMB names would be missing entirely.
        free(names); free(scanned); free(variants);
        return true;
    }

    // CTB. No ETag and gzip returns the full 111,887 bytes, so the transfer
    // cannot be avoided -- only hashed and compared at the end.
    if (ok) {
        hash_ctx_t hash = {.operation = PSA_HASH_OPERATION_INIT};
        // psa_crypto_init() is idempotent and already done by esp-tls, but the
        // refresh must not depend on a TLS session having been opened first.
        hash.usable = psa_crypto_init() == PSA_SUCCESS &&
                      psa_hash_setup(&hash.operation, PSA_ALG_SHA_256) == PSA_SUCCESS;

        builder.op_mask = TRANSIT_OP_MASK_CTB;
        transit_json_init(&parser, builder_member, builder_object_end, &builder);
        transit_http_options_t ctb_options = {
            .on_bytes = hash_bytes,
            .bytes_data = &hash,
            .timeout_ms = 30000,
        };
        status = transit_http_get_json_ex(NULL, CTB_ROUTE_TABLE, &parser, &ctb_options);
        if (status != TRANSIT_STATUS_OK) ok = false;

        size_t hash_length = 0;
        if (hash.usable) {
            if (psa_hash_finish(&hash.operation, ctb_hash_out, 32, &hash_length) != PSA_SUCCESS) {
                hash.usable = false;
            }
        }
        if (!hash.usable) {
            // Without a hash there is no change detector for CTB. Zero it so the
            // comparison reports "changed" rather than a false match.
            memset(ctb_hash_out, 0, 32);
            (void)psa_hash_abort(&hash.operation);
        }
    }

    if (!ok || builder.name_count == 0) {
        ESP_LOGW(TAG, "rebuild aborted; keeping previous index");
        free(names); free(scanned); free(variants);
        return false;
    }
    if (builder.overflow) {
        // The feeds outgrew the working caps. Refuse rather than install a
        // partial index that would grey out real routes.
        ESP_LOGW(TAG, "route lists exceed working caps; keeping previous index");
        free(names); free(scanned); free(variants);
        return false;
    }

    qsort(names, builder.name_count, sizeof(*names), compare_names);

    // Remap each scanned variant onto its sorted name position.
    uint16_t variant_count = 0;
    for (uint16_t i = 0; i < builder.variant_count; ++i) {
        uint16_t low = 0, high = builder.name_count;
        while (low < high) {
            const uint16_t mid = (uint16_t)(low + (high - low) / 2);
            if (memcmp(names[mid].name, scanned[i].route, TRANSIT_ROUTE_NAME_LEN) < 0) low = (uint16_t)(mid + 1);
            else high = mid;
        }
        if (low >= builder.name_count) continue;
        if (memcmp(names[low].name, scanned[i].route, TRANSIT_ROUTE_NAME_LEN) != 0) continue;
        variants[variant_count].name_index = low;
        variants[variant_count].bound = scanned[i].bound;
        variants[variant_count].service_type = scanned[i].service_type;
        variant_count++;
    }
    qsort(variants, variant_count, sizeof(*variants), compare_variants);
    free(scanned);

    const bool written = write_index_file(names, builder.name_count, variants, variant_count);
    if (written) {
        ESP_LOGI(TAG, "wrote refreshed index: %u names, %u variants (active next launch)",
                 (unsigned)builder.name_count, (unsigned)variant_count);
    }

    // Deliberately NOT installed live. This runs on the worker while the lookups
    // run on the LVGL task, and the table pointer and its count are two separate
    // words: swapping them here would let a keypad repaint read a new pointer with
    // an old count. The file is picked up by transit_index_load() at the next
    // launch, which is early enough for a list that changes a few times a year.
    free(names);
    free(variants);
    return written;
}

bool transit_index_maybe_refresh(bool online, bool foreground)
{
    // Online only, and never while the app is on screen: the keypad must not
    // change under the user, and the refresh must not compete with a lookup.
    if (!online || foreground) return false;

    const int32_t now = (int32_t)time(NULL);
    if (now < MIN_PLAUSIBLE_EPOCH) return false;   // clock unset; cannot schedule

    read_checked_at();
    if (s_checked_at != 0 && now - s_checked_at < KMB_INTERVAL_S) return false;

    char stored_etag[80] = {0};
    unsigned char stored_hash[32] = {0};
    int32_t ctb_checked = 0;
    nvs_handle_t handle = open_nvs(NVS_READONLY);
    if (handle != 0) {
        size_t length = sizeof(stored_etag);
        (void)nvs_get_str(handle, KEY_ETAG, stored_etag, &length);
        length = sizeof(stored_hash);
        (void)nvs_get_blob(handle, KEY_CTB_HASH, stored_hash, &length);
        (void)nvs_get_i32(handle, "tr.ctbchk", &ctb_checked);
        nvs_close(handle);
    }

    // CTB pays full price every time, so it is weekly while KMB is daily. When
    // the CTB window is closed and KMB reports 304, the check costs one
    // conditional GET and stops.
    const bool ctb_due = ctb_checked == 0 || now - ctb_checked >= CTB_INTERVAL_S;

    char new_etag[80] = {0};
    unsigned char new_hash[32] = {0};
    bool kmb_changed = false;
    ESP_LOGI(TAG, "checking route lists (ctb %s)", ctb_due ? "due" : "not due");
    const bool ok = rebuild(stored_etag[0] != '\0' ? stored_etag : NULL, ctb_due,
                            new_etag, sizeof(new_etag), new_hash, &kmb_changed);
    if (!ok) return true;   // the check ran and failed; try again next window

    // A rebuild happened when KMB changed, or when the weekly CTB window forced
    // one. Only then is there a new hash worth storing.
    const bool rebuilt = kmb_changed || ctb_due;
    const bool ctb_unchanged = ctb_due && memcmp(new_hash, stored_hash, sizeof(new_hash)) == 0;
    if (ctb_unchanged) ESP_LOGI(TAG, "ctb route list unchanged");

    handle = open_nvs(NVS_READWRITE);
    if (handle != 0) {
        if (new_etag[0] != '\0') (void)nvs_set_str(handle, KEY_ETAG, new_etag);
        if (rebuilt) {
            (void)nvs_set_blob(handle, KEY_CTB_HASH, new_hash, sizeof(new_hash));
            (void)nvs_set_i32(handle, "tr.ctbchk", now);
        }
        (void)nvs_commit(handle);
        nvs_close(handle);
    }
    store_checked_at(now);
    return true;
}
