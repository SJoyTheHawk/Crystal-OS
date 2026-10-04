# Hong Kong Bus App — Code Guide
**Date:** 2026-09-24  
**Status:** Draft  
**Purpose:** Implementation guide for Crystal OS Bus App v2 with architecture, threading, and data structures

---

## Document Organization

This code guide should be read alongside:
- [`bus-app-ui-design.md`](bus-app-ui-design.md) — UI/UX, screens, interaction patterns
- [`bus-app-workflow.md`](bus-app-workflow.md) — Data flow, caching strategy, pseudocode

---

## 1. Component Architecture

### 1.1 Component Layout

```
components/
├── bus_service/                    # Data layer (C, OS-owned)
│   ├── include/
│   │   ├── bus_service.h          # Public API (no LVGL, no C++)
│   │   ├── bus_routes.h           # Route index accessors
│   │   └── bus_cache.h            # Cache manager interface
│   ├── src/
│   │   ├── bus_service.c          # Worker task, HTTP, dispatcher
│   │   ├── bus_http.c             # HTTP client wrapper
│   │   ├── bus_json.c             # Incremental JSON parser
│   │   ├── bus_routes.c           # Route index + filtering
│   │   ├── bus_cache.c            # LittleFS cache manager
│   │   ├── bus_normalize.c        # KMB/CTB format converter
│   │   └── bus_index_data.c       # Generated: packed route index
│   └── tools/
│       └── gen_route_index.py     # Regenerates bus_index_data.c
│
└── bus_app/                        # UI layer (C++, app-owned)
    ├── include/
    │   ├── bus_app.hpp             # BusApp : CrystalApp
    │   └── bus_keypad.hpp          # Custom keypad widget
    ├── src/
    │   ├── bus_app.cpp             # Main app implementation
    │   ├── bus_keypad.cpp          # Scrollable keypad logic
    │   ├── bus_favorites.cpp       # Favorites page
    │   ├── bus_search.cpp          # Route search page
    │   ├── bus_stops.cpp           # Stop picker page
    │   ├── bus_eta.cpp             # ETA board page
    │   └── bus_icon.c              # Procedural icon (like clock_icon.c)
    └── CMakeLists.txt
```

**Design principle:** `bus_service` is pure C with no LVGL dependency, designed to be reusable by future Lua apps (Phase 17) or ABI façades (Phase 16).

---

## 2. Data Model

### 2.1 Core Structures (Fixed-size PODs)

```c
// bus_service.h

typedef enum {
    BUS_OP_KMB = 0,
    BUS_OP_CTB = 1,
    BUS_OP_NWFB = 2  // Future: New World First Bus
} bus_operator_t;

typedef enum {
    BUS_DIR_OUTBOUND = 'O',
    BUS_DIR_INBOUND = 'I'
} bus_direction_t;

// Route variant (one company + direction)
typedef struct {
    char route[5];              // "68X", NUL-terminated, max 4 chars
    uint8_t op;                 // bus_operator_t
    char bound;                 // 'O' or 'I' (normalized from CTB "outbound"/"inbound")
    uint8_t service_type;       // KMB only; always 1 for CTB
    char orig_en[48];           // Origin name (English)
    char dest_en[48];           // Destination name (English)
    char orig_tc[48];           // Origin name (Traditional Chinese)
    char dest_tc[48];           // Destination name (Traditional Chinese)
} bus_route_variant_t;          // 198 B

// Stop info (lazy-loaded)
typedef struct {
    char stop_id[20];           // Opaque operator ID (e.g. "HO06-W-1050-0")
    uint16_t seq;               // Sequence number on route (1-indexed)
    char name_en[60];           // Empty until resolved
    char name_tc[60];           // Empty until resolved
    float lat, lon;             // 0.0 until resolved
    bool resolved;              // Name fetched flag
} bus_stop_t;                   // 148 B

// ETA entry
typedef struct {
    uint32_t eta_epoch;         // Unix timestamp
    int32_t minutes_left;       // Calculated: (eta_epoch - now) / 60
    char remark_en[32];         // "Scheduled", "Last bus", etc.
    char remark_tc[32];         // Operator remark in TC
} bus_eta_t;                    // 72 B

// ETA result (up to 3 next buses)
typedef struct {
    bus_eta_t entries[3];
    uint8_t count;              // 0-3
    uint32_t fetched_at;        // When this was fetched (for age calc)
} bus_eta_result_t;             // 220 B
```

**Memory budget:**
- 200-stop route: 200 × 148 B = 29.6 KB (PSRAM)
- 8 favorites with cached ETAs: 8 × 220 B = 1.76 KB (NVS)

---

### 2.2 Route Name Index (Compact, in Flash)

```c
// bus_routes.h

// Alphabet: "0123456789ABCDEFGHKMNOPRSTWX" (28 chars)
#define BUS_ROUTE_CHARSET "0123456789ABCDEFGHKMNOPRSTWX"
#define BUS_ROUTE_CHARSET_LEN 28

// Packed index entry (sorted by name)
typedef struct {
    char name[4];               // NOT NUL-terminated, space-padded
    uint8_t ops;                // bit 0 = KMB, bit 1 = CTB
} bus_route_name_t;             // 5 B

// Generated data (in bus_index_data.c)
extern const bus_route_name_t bus_route_index[];
extern const uint16_t bus_route_index_count;  // 1,048 routes

// Query functions (allocation-free, LVGL task safe)
uint32_t bus_route_next_mask(const char *prefix, size_t prefix_len);
uint8_t bus_route_is_complete(const char *name, size_t len);
```

**Index size:** 1,048 routes × 5 B = 5,240 bytes (.rodata)

**Why space-padded, not NUL-terminated:**
- Exact 5-byte records with no padding holes
- `memcmp` over 4 bytes gives total order matching `strcmp`
- Space (0x20) sorts below all characters in the charset

---

## 3. Threading Model

### 3.1 Worker Task (Core 0)

```c
// bus_service.c

static TaskHandle_t s_worker_task = NULL;
static QueueHandle_t s_request_queue = NULL;

// Created on first use, not at boot
void bus_service_init(void) {
    if (s_worker_task != NULL) return;
    
    s_request_queue = xQueueCreate(4, sizeof(bus_request_t));
    xTaskCreatePinnedToCore(
        bus_worker_task,
        "bus_worker",
        8192,           // Stack size (TLS + JSON parser needs >4KB)
        NULL,
        2,              // Priority (same as crystal_service)
        &s_worker_task,
        0               // Core 0
    );
}
```

**Rules:**
- One request in flight at a time
- Queue depth = 4, full queue rejects (never blocks LVGL task)
- Cancellation honored within one in-flight request
- `bus_service_suspend(bool)` stops new requests (for OTA, Phase 12)

---

### 3.2 Result Delivery (Custom Path, Not crystal_core Events)

**Why not use `UI_EVT_*`?**
- `EventMessage` has 64-byte payload (`kEventDataMax`)
- Stop lists and ETA results don't fit
- Widening the union costs every queued event in the firmware

**Solution: Listener pattern**

```c
// bus_service.h

typedef enum {
    BUS_EVT_ROUTE_VARIANTS,     // Multiple directions/companies
    BUS_EVT_STOPS_LIST,         // Route-stop sequence
    BUS_EVT_STOP_DETAIL,        // Single stop name resolved
    BUS_EVT_ETA,                // ETA for one stop
    BUS_EVT_ERROR               // Network/parse error
} bus_event_type_t;

typedef struct {
    bus_event_type_t type;
    uint32_t request_id;        // Monotonic, for stale result detection
    esp_err_t status;
    union {
        struct {
            bus_route_variant_t *variants;
            uint8_t count;
        } route_variants;
        struct {
            bus_stop_t *stops;
            uint16_t count;
        } stops_list;
        struct {
            uint16_t seq;       // Which stop was resolved
            bus_stop_t stop;
        } stop_detail;
        struct {
            char stop_id[20];
            bus_eta_result_t result;
        } eta;
        struct {
            char message[64];
        } error;
    } data;
} bus_event_t;

// Called on LVGL task, from service's own drain timer
typedef void (*bus_listener_t)(const bus_event_t *event, void *user_data);

// Install listener (only one at a time)
void bus_service_set_listener(bus_listener_t cb, void *user_data);
```

**Lifecycle:**
- `BusApp::onCreate()` → install listener
- `BusApp::onDestroy()` → `bus_service_set_listener(NULL, NULL)`
- Results can arrive after app destroyed → listener checks request IDs

---

## 4. HTTP Layer

### 4.1 Client Configuration

```c
// bus_http.c

#include "esp_http_client.h"
#include "esp_crt_bundle.h"

esp_http_client_config_t config = {
    .url = url,
    .method = HTTP_METHOD_GET,
    .timeout_ms = 5000,
    .crt_bundle_attach = esp_crt_bundle_attach,
    .keep_alive_enable = true,      // Reuse TLS session
    .buffer_size = 4096,            // Read window size
};
```

**Critical: Use open/fetch_headers/read/close, NOT perform()**

```c
// WRONG (perform() consumes the body)
esp_http_client_perform(client);
char buf[1024];
esp_http_client_read(client, buf, sizeof(buf));  // Always returns 0!

// CORRECT (manual fetch loop)
esp_http_client_open(client, 0);
esp_http_client_fetch_headers(client);
int content_length = esp_http_client_get_content_length(client);

while (true) {
    int read_len = esp_http_client_read(client, buffer, buffer_size);
    if (read_len <= 0) break;
    // Process buffer with incremental parser
}

// IMPORTANT: Don't call close() if keep-alive is needed
// esp_http_client_close(client);  // Kills TLS session!
```

See memory fact: [`esp-http-client-close-kills-tls.md`](../memory/esp-http-client-close-kills-tls.md)

---

### 4.2 API Endpoints

```c
// bus_http.c

#define KMB_BASE_URL  "https://data.etabus.gov.hk/v1/transport/kmb"
#define CTB_BASE_URL  "https://rt.data.gov.hk/v2/transport/citybus"

// Route variants (for direction chooser)
// GET /v1/transport/kmb/route/{route}
// GET /v2/transport/citybus/route/CTB/{route}

// Route-stop mapping (sequence + stop IDs)
// GET /v1/transport/kmb/route-stop/{route}/{bound}/{service_type}
// GET /v2/transport/citybus/route-stop/CTB/{route}/{bound}

// Stop detail (name, coordinates)
// GET /v1/transport/kmb/stop/{stop_id}
// GET /v2/transport/citybus/stop/CTB/{stop_id}

// ETA (real-time)
// GET /v1/transport/kmb/eta/{stop_id}/{route}/{service_type}
// GET /v2/transport/citybus/eta/CTB/{stop_id}/{route}
```

**Keep-alive strategy:**
- Resolving 60-200 stop names: reuse one client handle
- Measured: ~100ms per stop with keep-alive vs 600-900ms without
- Server may close between requests (open question 2) → measure and cap stops if needed

---

### 4.3 TLS Memory Optimization

**Problem:** mbedTLS allocates from DRAM by default, OOMs with framebuffers up

**Solution:** Use PSRAM for mbedTLS internal buffers

```c
// sdkconfig or menuconfig
CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y
CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=4096  // Down from 16384
CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=4096
```

See memory fact: [`mbedtls-internal-mem-alloc-ooms.md`](../memory/mbedtls-internal-mem-alloc-ooms.md)

---

## 5. Data Normalization (KMB ↔ CTB)

### 5.1 Format Differences

| Field | KMB | CTB/NWFB | Normalization |
|-------|-----|----------|---------------|
| Route ID | `"1"` | `"1"` | Direct copy |
| Stop ID | `"HO06-W-1050-0"` | `"ho06-w-1050-0"` | `to_uppercase()` |
| Direction | `"O"`, `"I"` | `"outbound"`, `"inbound"` | Map to `'O'`, `'I'` |
| Coordinates | `"lat"`, `"long"` | `"lat"`, `"lng"` | Rename `lng` → `long` |
| Service type | Present | Always `"1"` | Default to 1 if missing |

### 5.2 Normalization Functions

```c
// bus_normalize.c

// Convert CTB stop ID to uppercase for unified cache key
void bus_normalize_stop_id(char *stop_id) {
    for (char *p = stop_id; *p; ++p) {
        *p = toupper((unsigned char)*p);
    }
}

// Map CTB direction strings to single char
char bus_normalize_direction(const char *dir_str, bus_operator_t op) {
    if (op == BUS_OP_KMB) {
        return dir_str[0];  // Already "O" or "I"
    }
    // CTB uses "outbound"/"inbound"
    if (strcmp(dir_str, "outbound") == 0) return 'O';
    if (strcmp(dir_str, "inbound") == 0) return 'I';
    return 'O';  // Default
}

// Extract service_type (KMB has it, CTB doesn't)
uint8_t bus_normalize_service_type(const cJSON *json, bus_operator_t op) {
    if (op == BUS_OP_KMB) {
        cJSON *st = cJSON_GetObjectItem(json, "service_type");
        return st ? (uint8_t)st->valueint : 1;
    }
    return 1;  // CTB always 1
}
```

**Reference:** Solutions from [hk-bus-crawling normalization logic](https://github.com/hkbus/hk-bus-crawling)

---

## 6. Cache Manager

### 6.1 Storage Strategy

**Decision (from open questions):**
- **Compression:** gzip (best ecosystem support, S3 ROM has `tinfl_decompress`)
- **Storage:** LittleFS partition (not NVS, exceeds 2048-byte value limit)
- **Multilingual:** User-selectable language only (store either EN or TC, not both)
- **Favorites:** Store route IDs only, fetch names on load
- **Offline mode:** Show cached routes when WiFi down, no ETA

### 6.2 Cache Structure

```
/littlefs/
└── bus/
    ├── routes.json.gz          # All route names (weekly refresh)
    ├── stops.json.gz           # All stops for active routes (session cache)
    └── metadata.json           # Version, last_update timestamp
```

### 6.3 Cache Manager API

```c
// bus_cache.h

// Initialize cache (mount LittleFS if needed)
esp_err_t bus_cache_init(void);

// Check if cache exists and is valid
bool bus_cache_is_valid(const char *cache_name, uint32_t max_age_sec);

// Save compressed JSON to cache
esp_err_t bus_cache_save(const char *cache_name, const char *json_data, size_t len);

// Load and decompress from cache
esp_err_t bus_cache_load(const char *cache_name, char **out_data, size_t *out_len);

// Free loaded cache data
void bus_cache_free(char *data);

// Clear all cache files
esp_err_t bus_cache_clear_all(void);
```

### 6.4 Compression/Decompression

```c
// bus_cache.c

#include "miniz.h"  // ESP-IDF ROM exports tinfl_decompress

// Compress with gzip header
esp_err_t bus_cache_compress(const char *input, size_t input_len,
                              uint8_t **output, size_t *output_len) {
    mz_ulong compressed_len = mz_compressBound(input_len);
    uint8_t *compressed = heap_caps_malloc(compressed_len, MALLOC_CAP_SPIRAM);
    
    if (mz_compress(compressed, &compressed_len,
                    (const uint8_t*)input, input_len) != MZ_OK) {
        free(compressed);
        return ESP_FAIL;
    }
    
    *output = compressed;
    *output_len = compressed_len;
    return ESP_OK;
}

// Decompress gzip stream
esp_err_t bus_cache_decompress(const uint8_t *input, size_t input_len,
                                char **output, size_t *output_len) {
    // Estimate decompressed size (from metadata or guess 10x)
    size_t decomp_size = input_len * 10;
    char *decompressed = heap_caps_malloc(decomp_size, MALLOC_CAP_SPIRAM);
    
    mz_ulong actual_size = decomp_size;
    if (mz_uncompress((uint8_t*)decompressed, &actual_size,
                      input, input_len) != MZ_OK) {
        free(decompressed);
        return ESP_FAIL;
    }
    
    *output = decompressed;
    *output_len = actual_size;
    return ESP_OK;
}
```

---

## 7. JSON Parser (Incremental, Bounded)

### 7.1 Why Not cJSON?

- **cJSON builds full DOM** → 3MB dataset becomes 600KB-1.5MB in RAM
- **Weather's `strstr` approach doesn't scale** → can't tell which object a key belongs to in arrays

### 7.2 Streaming Parser Design

```c
// bus_json.h

#define BUS_JSON_WINDOW_SIZE 4096

typedef struct {
    char window[BUS_JSON_WINDOW_SIZE];
    size_t window_fill;
    int depth;                      // Brace/bracket nesting level
    bool in_string;
    bool escape_next;
} bus_json_parser_t;

// Initialize parser
void bus_json_init(bus_json_parser_t *parser);

// Feed data chunk (call multiple times)
esp_err_t bus_json_feed(bus_json_parser_t *parser,
                         const char *data, size_t len,
                         bus_json_callback_t cb, void *user_data);

// Callback receives key-value pairs for current object
typedef void (*bus_json_callback_t)(const char *key,
                                     const char *value,
                                     void *user_data);
```

### 7.3 Safety Rules

```c
// ALWAYS bounds-check when copying JSON values
void bus_json_copy_string(char *dest, size_t dest_size,
                          const char *src) {
    strlcpy(dest, src, dest_size);  // NUL-terminates, never overruns
}

// Truncate long values, never wrap or allocate
if (strlen(value) >= sizeof(variant.dest_en)) {
    ESP_LOGW(TAG, "Truncating dest_en: %zu chars", strlen(value));
}
bus_json_copy_string(variant.dest_en, sizeof(variant.dest_en), value);
```

**Why:** Third-party data on a device with no MMU. A 3,000-char `dest_en` must truncate, not corrupt the heap.

---

## 8. Persistent State (NVS via CrystalState)

### 8.1 State Keys

```c
// bus_app.cpp

// CrystalState keys (max 7 chars, values ≤ 2048 bytes)
#define KEY_FAVORITES   "favs"      // Packed: route_id + stop_id per favorite (8×40B = 320B)
#define KEY_FAV_NAMES   "favnm"     // Cached stop names for instant paint (8×60B = 480B)
#define KEY_FAV_ETAS    "faveta"    // Last known ETAs (8×220B = 1760B)
#define KEY_RECENT      "recent"    // Last 6 route names (6×5B = 30B)
#define KEY_LAST_ROUTE  "lastrt"    // Route in flight when paused (5B)
#define KEY_LAST_STOP   "laststp"   // Stop ID when paused (20B)
#define KEY_ACTIVE_TAB  "tab"       // Which tab was active (1B)
#define KEY_LANGUAGE    "lang"      // "en" or "tc" (3B)
```

**All keys in `bus` namespace** → Phase 13's `CrystalState::clear()` wipes completely

### 8.2 Favorite Structure (Packed Binary)

```c
// Stored as binary blob, not JSON (saves space)
typedef struct {
    char route[5];
    uint8_t op;
    char bound;
    uint8_t service_type;
    char stop_id[20];
    char stop_name_en[60];  // Cached for instant display
    char stop_name_tc[60];
} __attribute__((packed)) bus_favorite_entry_t;  // 146 B

// Max 8 favorites = 8 × 146 B = 1168 B (well under 2048B limit)
#define MAX_FAVORITES 8
```

---

## 9. UI Layer (C++ / LVGL)

### 9.1 BusApp Class Structure

```cpp
// bus_app.hpp

class BusApp : public CrystalApp {
public:
    BusApp();
    ~BusApp() override;

    // Lifecycle (dispatched)
    void onCreate() override;
    void onPause() override;
    void onResume() override;
    void onDestroy() override;
    bool onBack() override;

private:
    // Pages
    lv_obj_t *favorites_page_;
    lv_obj_t *search_page_;
    lv_obj_t *stop_picker_page_;    // Modal
    lv_obj_t *eta_board_page_;      // Modal

    // Widgets
    lv_obj_t *tab_bar_;
    BusKeypad *keypad_;             // Custom widget
    lv_obj_t *favorite_list_;
    lv_obj_t *stop_list_;
    lv_obj_t *eta_container_;

    // State
    uint32_t current_request_id_;
    bus_route_variant_t current_route_;
    char current_stop_id_[20];
    bus_favorite_entry_t favorites_[MAX_FAVORITES];
    uint8_t favorites_count_;
    
    // Timers
    lv_timer_t *eta_refresh_timer_;
    lv_timer_t *result_drain_timer_;

    // Callbacks
    static void on_bus_event(const bus_event_t *event, void *user_data);
    static void on_eta_refresh_timer(lv_timer_t *timer);
    static void on_favorite_clicked(lv_event_t *e);
    static void on_route_submitted(lv_event_t *e);
    static void on_stop_selected(lv_event_t *e);
    static void on_save_clicked(lv_event_t *e);

    // Helpers
    void build_favorites_page();
    void build_search_page();
    void build_stop_picker(const bus_route_variant_t *route);
    void build_eta_board(const char *stop_id);
    void update_eta_display(const bus_eta_result_t *result);
    void show_error(const char *message);
    void load_favorites_from_nvs();
    void save_favorites_to_nvs();
};
```

### 9.2 Custom Keypad Widget

```cpp
// bus_keypad.hpp

class BusKeypad {
public:
    BusKeypad(lv_obj_t *parent);
    ~BusKeypad();

    void set_callback(void (*cb)(const char *route, void *user_data), void *user_data);
    void clear();
    const char *get_input() const { return input_; }

private:
    lv_obj_t *container_;
    lv_obj_t *display_;             // Input display label
    lv_obj_t *number_grid_;         // Fixed 4×3 grid for 0-9
    lv_obj_t *letter_strip_;        // Horizontal scroll for A-Z
    lv_obj_t *backspace_btn_;
    lv_obj_t *enter_btn_;
    lv_obj_t *recent_container_;    // Recent routes chips

    char input_[5];                 // Current input (max 4 + NUL)
    void (*submit_callback_)(const char *route, void *user_data);
    void *user_data_;

    // Adaptive greying logic
    void update_key_states();
    void on_key_pressed(char c);
    void on_backspace();
    void on_enter();

    static void key_event_cb(lv_event_t *e);
    static void enter_event_cb(lv_event_t *e);
};
```

### 9.3 Keypad Implementation (Scrollable Letter Strip)

```cpp
// bus_keypad.cpp

void BusKeypad::build_letter_strip() {
    // Horizontal scroll container
    letter_strip_ = lv_obj_create(container_);
    lv_obj_set_size(letter_strip_, LV_PCT(100), 64);
    lv_obj_set_flex_flow(letter_strip_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(letter_strip_, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(letter_strip_, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(letter_strip_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_column(letter_strip_, 8, 0);
    lv_obj_center(letter_strip_);  // Center align

    // Create buttons for each letter
    const char *letters = BUS_ROUTE_CHARSET + 10;  // Skip "0123456789"
    for (const char *p = letters; *p; ++p) {
        lv_obj_t *btn = lv_btn_create(letter_strip_);
        lv_obj_set_size(btn, 56, 56);
        lv_obj_add_event_cb(btn, key_event_cb, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(btn, (void*)(intptr_t)*p);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text_fmt(label, "%c", *p);
        lv_obj_center(label);
    }
}

void BusKeypad::update_key_states() {
    // Get mask of allowed next characters
    uint32_t mask = bus_route_next_mask(input_, strlen(input_));
    
    // Update letter buttons
    lv_obj_t *btn = lv_obj_get_child(letter_strip_, 0);
    int idx = 10;  // Start after digits in charset
    while (btn) {
        bool is_allowed = (mask & (1 << idx)) != 0;
        lv_obj_set_style_opa(btn, is_allowed ? LV_OPA_COVER : LV_OPA_40, 0);
        // NOTE: Do NOT disable! Greyed keys stay tappable (index may be stale)
        btn = lv_obj_get_next_sibling(btn);
        idx++;
    }
    
    // Update enter button
    uint8_t is_complete = bus_route_is_complete(input_, strlen(input_));
    lv_obj_set_style_opa(enter_btn_, is_complete ? LV_OPA_COVER : LV_OPA_40, 0);
}
```

**Key design decisions:**
- Fixed number grid (muscle memory)
- Scrollable letters (reduces overwhelm: 6-7 visible vs 18 in grid)
- Greyed keys stay tappable (index may be stale, API is authority)
- Center-aligned layout (better visual balance)

---

## 10. Lifecycle Integration

### 10.1 onCreate (≤80ms budget)

```cpp
void BusApp::onCreate() {
    // 1. Build UI tree (from cache, no network)
    build_favorites_page();
    build_search_page();
    
    // 2. Load favorites from NVS
    load_favorites_from_nvs();
    
    // 3. Paint favorites with cached ETAs (instant display)
    for (uint8_t i = 0; i < favorites_count_; ++i) {
        add_favorite_card(&favorites_[i]);
    }
    
    // 4. Install listener for background updates
    bus_service_set_listener(on_bus_event, this);
    
    // 5. Request fresh ETAs in background (non-blocking)
    for (uint8_t i = 0; i < favorites_count_; ++i) {
        uint32_t req_id = bus_service_request_eta(
            favorites_[i].stop_id,
            favorites_[i].route,
            favorites_[i].op,
            favorites_[i].service_type
        );
    }
    
    // 6. Position root at (0, 0) NOT area.x1/y1
    lv_obj_set_pos(root_, 0, 0);
}
```

**Rules:**
- Never block on network
- Paint from cache immediately
- Refresh in background

---

### 10.2 onPause

```cpp
void BusApp::onPause() {
    // 1. Delete all timers
    if (eta_refresh_timer_) {
        lv_timer_del(eta_refresh_timer_);
        eta_refresh_timer_ = nullptr;
    }
    
    // 2. Cancel in-flight requests
    bus_service_cancel_all();
    
    // 3. Save current state
    save_favorites_to_nvs();
    state().putString(KEY_LAST_ROUTE, current_route_.route);
    state().putString(KEY_LAST_STOP, current_stop_id_);
    state().putInt(KEY_ACTIVE_TAB, get_active_tab());
}
```

---

### 10.3 onDestroy

```cpp
void BusApp::onDestroy() {
    // 1. Uninstall listener FIRST (prevent late events)
    bus_service_set_listener(NULL, NULL);
    
    // 2. Cancel all requests
    bus_service_cancel_all();
    
    // 3. NULL all widget pointers (defense against late events)
    favorites_page_ = nullptr;
    search_page_ = nullptr;
    stop_picker_page_ = nullptr;
    eta_board_page_ = nullptr;
    eta_container_ = nullptr;
    
    // 4. Free keypad
    delete keypad_;
    keypad_ = nullptr;
}
```

**Critical:** Results can arrive after destroy. Listener checks must validate request IDs and widget pointers.

---

## 11. Performance Gates

| Metric | Target | Measurement Method |
|--------|--------|--------------------|
| First render | ≤80ms | `onCreate()` → `lv_obj_invalidate()` |
| Keypad response | ≤16ms per keystroke | `on_key_pressed()` → `update_key_states()` |
| Stop list paint | First 20 rows <200ms | Request → `lv_list_add()` × 20 |
| ETA fetch | <2s (network dependent) | Request → callback |
| Peak PSRAM | ≤400 KB | `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` |
| Peak DRAM heap | ≤60 KB | `esp_get_free_heap_size()` |
| Memory after destroy | Returns to pre-launch | Watermark before/after |

---

## 12. Registration

```cpp
// main/main.cpp

#include "bus_app.hpp"

static CrystalApp *make_bus_app() {
    return new BusApp();
}

static const CrystalAppEntry kApps[] = {
    {"dev_tester", make_dev_tester_app, true, 0},
    {"clock",      make_clock_app,      true, 2},
    {"weather",    make_weather_app,    true, 3},
    {"calculator", make_calculator_app, true, 4},
    {"bus",        make_bus_app,        true, 5},
};
```

```cmake
# main/CMakeLists.txt
idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES crystal_shell bus_app
)
```

---

## 13. Open Questions Resolved

| Question | Decision | Rationale |
|----------|----------|-----------|
| Compression format | **gzip** | S3 ROM has `tinfl_decompress`, best ecosystem support |
| Incremental updates | **Fetch all, not delta** | Small system, delta sync unstable, full fetch more reliable |
| Multilingual | **User-selectable, not both stored** | Add Settings option, store only active language |
| Favorites storage | **Route IDs, not full objects** | Fetch names on load, saves NVS space (40B vs 146B per favorite) |
| Offline mode | **Yes, show cached routes, no ETA** | Degrade gracefully, keep last known data visible |

---

## 14. What This Does NOT Do

**Explicitly out of scope for v1:**
- ❌ Route table on device (only 1,048 route names in 5KB index)
- ❌ Fuzzy search / suggestions (exact match only, keypad prevents typos)
- ❌ Stop search across territory (needs 6,600-stop index, ~1.5MB)
- ❌ Map launch (coordinates as text only, no `openMaps()` equivalent)
- ❌ In-app language switch UI (Settings only, requires TC font assets 0.6-2.5MB)
- ❌ NLB, GMB, MTR bus (KMB/LWB + CTB only, matching reference app)

---

## 15. References

- [`bus-app-ui-design.md`](bus-app-ui-design.md) — UI/UX, screens, interaction patterns
- [`bus-app-workflow.md`](bus-app-workflow.md) — Data flow, caching strategy
- [hk-bus-crawling](https://github.com/hkbus/hk-bus-crawling) — Normalization patterns
- [hk-independent-bus-eta](https://github.com/hkbus/hk-independent-bus-eta) — Design reference
- [KMB API Specification](https://data.etabus.gov.hk/datagovhk/kmb_eta_api_specification.pdf)
- Crystal OS memory facts: `esp-http-client-close-kills-tls.md`, `mbedtls-internal-mem-alloc-ooms.md`

---

**Next Steps:**
1. Prototype `bus_service` HTTP + JSON parser (measure memory watermarks)
2. Build `bus_keypad` scrollable widget (validate UX on device)
3. Implement favorites page → ETA board flow (the 80% use case)
4. Add route search + stop picker (discovery flow)
5. Measure all performance gates, record results
