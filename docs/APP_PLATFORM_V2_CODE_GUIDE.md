# Crystal OS App Platform — v2 Code Guide

**Version:** 2.0  
**Status:** Phase 14 closed; Phases 15–17 pending  
**Date:** 2026-11-11  
**Audience:** Developers writing Phase 14-17 code, agents implementing the architecture

---

## 1. Project Structure and Build Setup

### 1.1 App Folder Layout

```
com.example.clock3p/
├── app.yaml                    # Manifest (source; author writes this)
├── CMakeLists.txt             # Build script (if C/native)
├── Makefile                   # Or use CMake
├── src/
│   ├── main.lua               # Lua entry point (script tier)
│   └── main.cpp               # C++ entry point (native tier)
├── assets/
│   ├── icon.bin               # LVGL binary image (96x96 or 128x128)
│   ├── sounds/
│   │   └── chime.wav
│   └── images/
│       └── background.bin
└── data/                      # Created at runtime; don't version control
    └── state.json
```

### 1.2 Manifest File (app.yaml)

```yaml
# Essential metadata
abi: 1
id: com.example.clock3p
name: Clock
version: 1.0.0
min_os: 1.0.0
author: Example Co.
license: MIT
description: "A simple clock app with timer and alarm"

# Runtime
runtime: lua                    # lua | declarative | native
entry: src/main.lua           # Module file or entry function
icon: assets/icon.bin         # LVGL binary image

# Visual
bar_style: dark               # Status bar contrast: dark | light

# Resources (admission gates)
memory_kb: 160                # Peak PSRAM usage
storage_kb: 32                # Persistent data quota
worker_threads: 0             # Background tasks (0-4)

# Permissions (absent = denied)
permissions:
  notify: true
  audio: false
  system_timer: true
  network_hosts: []           # Empty = no network access
  location: false
```

### 1.3 Build Command (crystal pack)

```bash
# Pack and sign app
crystal pack ./com.example.clock3p/
  # Reads app.yaml → app.json (convert + validate)
  # Hashes each file, signs with developer key
  # Emits: com.example.clock3p-1.0.0.capp

# Install over USB (unsigned, dev mode only)
crystal install --usb ./com.example.clock3p/
  # Opens pairing dialog if not yet paired
  # Streams folder contents to device
  # Device writes to /apps/.staging/
  # On completion, device verifies and renames to /apps/<id>/

# Run: pack + install + launch + log stream
crystal run ./com.example.clock3p/
```

---

## 2. Script Tier: Lua Implementation

### 2.1 Minimal Lua App (Clock)

```lua
-- src/main.lua
-- Minimal app that displays current time

local screen_handle = nil

function onCreate()
    -- Build UI tree
    screen_handle = crystal.ui.screen()
    
    local container = crystal.ui.container(screen_handle, {
        flex_direction = "column",
        gap = 16,
    })
    
    local time_label = crystal.ui.label(container, {
        text = "00:00",
        font = "large",
    })
    
    local refresh_timer = crystal.ui.timer(1000, function()
        local now = crystal.service.time.now()
        local formatted = crystal.service.time.format("%H:%M:%S")
        crystal.ui.set_text(time_label, formatted)
    end)
    
    return true
end

function onResume()
    -- App is visible; start timers
    return true
end

function onPause()
    -- Save state (clock has no persistent state)
    return true
end

function onDestroy()
    -- Cleanup happens automatically; timers deleted
    return true
end

return {
    onCreate = onCreate,
    onResume = onResume,
    onPause = onPause,
    onDestroy = onDestroy,
}
```

### 2.2 Stateful App (Favorites)

```lua
-- src/main.lua
-- App with persistent favorites list

local favorites = {}
local fav_list_handle = nil
local current_request_id = nil

function onCreate()
    -- Load persistent state
    favorites = crystal.state.get("fav_list") or {}
    
    -- Build UI
    screen_handle = crystal.ui.screen()
    
    local title = crystal.ui.label(screen_handle, {
        text = "My Favorites",
        font = "large",
    })
    
    fav_list_handle = crystal.ui.list(screen_handle, {
        item_height = 44,
    })
    
    -- Populate list
    for i, fav in ipairs(favorites) do
        crystal.ui.list_add(fav_list_handle, fav.name)
    end
    
    return true
end

function onResume()
    return true
end

function onPause()
    -- Save state before destruction
    crystal.state.set("fav_list", favorites)
    crystal.state.flush()  -- Force write to disk
    return true
end

function onDestroy()
    -- All handles dead; cleanup implicit
    return true
end

function onBack()
    -- Handle back gesture (optional)
    return false  -- Pass to next app if not consumed
end

return {
    onCreate = onCreate,
    onResume = onResume,
    onPause = onPause,
    onDestroy = onDestroy,
    onBack = onBack,
}
```

### 2.3 Async HTTP App (Weather)

```lua
-- src/main.lua
-- App that fetches weather data asynchronously

local weather_data = nil
local request_id = nil
local temp_label = nil

function onCreate()
    screen_handle = crystal.ui.screen()
    
    temp_label = crystal.ui.label(screen_handle, {
        text = "Loading...",
        font = "large",
    })
    
    local refresh_btn = crystal.ui.button(screen_handle, {
        text = "Refresh",
        on_click = function()
            fetchWeather()
        end,
    })
    
    -- Fetch on startup
    fetchWeather()
    
    return true
end

function fetchWeather()
    request_id = crystal.service.http.fetch(
        "https://api.open-meteo.com/v1/forecast?latitude=22.3&longitude=114.2&current=temperature_2m",
        {
            method = "GET",
            timeout_sec = 10,
        },
        function(req_id, status, body)
            -- Callback fires in LVGL task
            if req_id ~= request_id then
                return  -- Stale response; discard
            end
            
            if status ~= 200 then
                crystal.ui.set_text(temp_label, "Error: " .. status)
                return
            end
            
            -- Parse JSON with bundled cjson
            local data = crystal.json.decode(body)
            if not data then
                crystal.ui.set_text(temp_label, "Parse error")
                return
            end
            
            weather_data = data
            local temp = data.current.temperature_2m
            crystal.ui.set_text(temp_label, string.format("%.1f°C", temp))
        end
    )
end

function onResume()
    return true
end

function onPause()
    -- Cancel any in-flight request before destruction
    if request_id then
        crystal.service.http.cancel(request_id)
        request_id = nil
    end
    return true
end

function onDestroy()
    -- Host cancels request and suppresses callback automatically
    return true
end

return {
    onCreate = onCreate,
    onResume = onResume,
    onPause = onPause,
    onDestroy = onDestroy,
}
```

### 2.4 Rich List with Icons

```lua
-- src/main.lua
-- App demonstrating rich list items with icons + primary + secondary text

local routes = {
    {id = "kmb_001", name = "Route 1", desc = "Central → Tin Shui Wai", icon = "assets/bus.bin"},
    {id = "ctb_002", name = "Route 2", desc = "Admiralty → Causeway Bay", icon = "assets/bus.bin"},
}

function onCreate()
    screen_handle = crystal.ui.screen()
    
    local list = crystal.ui.list(screen_handle, {
        item_height = 60,  -- Taller for two-line items
    })
    
    for i, route in ipairs(routes) do
        crystal.ui.list_add(list, {
            icon = route.icon,
            primary = route.name,
            secondary = route.desc,
            on_click = function()
                showRouteDetail(route)
            end
        })
    end
    
    return true
end

function showRouteDetail(route)
    -- Navigate to detail page (state machine pattern)
    current_route = route
    state = "detail"
    updateUI()
end

return {onCreate = onCreate}
```

### 2.5 Animation Example

```lua
-- src/main.lua
-- Demonstrates fade-in, fade-out, and highlight animations

local card_handle = nil

function onCreate()
    screen_handle = crystal.ui.screen()
    
    card_handle = crystal.ui.container(screen_handle, {
        flex_direction = "column",
        gap = 8,
    })
    
    crystal.ui.set_style(card_handle, {
        bg_color = 0x2E3440,  -- Dark gray
        radius = 12,
        padding = 16,
        opacity = 0,  -- Start hidden
    })
    
    local label = crystal.ui.label(card_handle, {
        text = "Welcome to Crystal OS",
        font = "large",
    })
    
    local btn = crystal.ui.button(card_handle, {
        text = "Highlight Me",
        on_click = function()
            animateHighlight(btn)
        end,
    })
    
    -- Fade in on onCreate
    crystal.ui.anim(card_handle, {opacity = 255}, 300, {
        easing = "ease_out",
    })
    
    return true
end

function animateHighlight(handle)
    -- Highlight: flash color, then fade back
    local original_color = 0x2E3440
    
    crystal.ui.anim(handle, {bg_color = 0x4A90D9}, 150, {
        easing = "ease_in_out",
        on_done = function()
            crystal.ui.anim(handle, {bg_color = original_color}, 300, {
                easing = "ease_in_out",
            })
        end
    })
end

function onPause()
    -- Fade out before destruction (optional; most apps skip this)
    crystal.ui.anim(card_handle, {opacity = 0}, 200, {
        easing = "ease_in",
    })
    return true
end

return {onCreate = onCreate, onPause = onPause}
```

### 2.6 Dialog and Spinner

```lua
-- src/main.lua
-- Confirmation dialog and loading spinner

local spinner_handle = nil

function onCreate()
    screen_handle = crystal.ui.screen()
    
    local delete_btn = crystal.ui.button(screen_handle, {
        text = "Delete All",
        on_click = function()
            showConfirmDialog()
        end,
    })
    
    local load_btn = crystal.ui.button(screen_handle, {
        text = "Load Data",
        on_click = function()
            loadDataWithSpinner()
        end,
    })
    
    return true
end

function showConfirmDialog()
    crystal.ui.dialog({
        title = "Confirm Delete",
        message = "This will delete all your data. Continue?",
        buttons = {
            {text = "Cancel", on_click = function() end},
            {text = "Delete", on_click = function()
                -- Perform deletion
                crystal.state.set("favorites", {})
                crystal.service.notify.toast("Data deleted")
            end},
        }
    })
end

function loadDataWithSpinner()
    -- Show spinner while loading
    spinner_handle = crystal.ui.spinner(screen_handle, {
        size = 32,
        color = 0x4A90D9,
    })
    
    -- Simulate async load with timer
    crystal.ui.after(2000, function()
        crystal.ui.delete(spinner_handle)
        spinner_handle = nil
        crystal.service.notify.toast("Data loaded")
    end)
end

function onDestroy()
    if spinner_handle then
        crystal.ui.delete(spinner_handle)
    end
end

return {onCreate = onCreate, onDestroy = onDestroy}
```

### 2.7 Scroll View for Long Content

```lua
-- src/main.lua
-- Settings page with scrollable mixed-height content

function onCreate()
    screen_handle = crystal.ui.screen()
    
    -- Scrollable container for settings rows
    local scroll = crystal.ui.scroll_view(screen_handle, {
        width = 480,
        height = 440,  -- Leave 40px for status bar
    })
    
    -- Add settings rows (different heights)
    crystal.ui.label(scroll, {
        text = "Display",
        font = "large",
    })
    
    local brightness_slider = crystal.ui.slider(scroll, {
        value = 80,
        min = 0,
        max = 100,
        on_change = function(val)
            crystal.state.set("brightness", val)
        end,
    })
    
    crystal.ui.label(scroll, {
        text = "Network",
        font = "large",
    })
    
    local wifi_switch = crystal.ui.switch(scroll, {
        value = true,
        on_change = function(enabled)
            -- Handle WiFi toggle
        end,
    })
    
    -- Long description text (wraps, variable height)
    crystal.ui.label(scroll, {
        text = "When enabled, the device will automatically connect to saved networks. This may consume more power.",
        font = "small",
        color = 0x808080,  -- Gray subtitle
    })
    
    return true
end

return {onCreate = onCreate}
```

### 2.8 Common Patterns: List with Callbacks

```lua
-- Assume app.json is read by host and available as _APP_MANIFEST
-- or passed to app at init time

function validateManifest()
    local manifest = crystal.system.info().app_manifest  -- or similar
    
    -- Verify ABI compatibility
    if manifest.abi ~= 1 then
        error("Incompatible ABI version")
    end
    
    -- Check permissions at runtime (advisory; host already enforced)
    if not manifest.permissions.network_hosts then
        -- App can't make HTTP requests
        -- Skip network code
    end
    
    return true
end
```

### 2.5 Common Patterns: List with Callbacks

```lua
function onCreate()
    screen_handle = crystal.ui.screen()
    
    local list = crystal.ui.list(screen_handle, {
        item_height = 44,
    })
    
    -- Add items
    crystal.ui.list_add(list, "Item 1")
    crystal.ui.list_add(list, "Item 2")
    
    -- Register callback on list
    crystal.ui.on(list, "click", function(index)
        print("Item clicked: " .. index)
        -- Handle click
    end)
    
    return true
end
```

### 2.9 Common Patterns: Tab View

```lua
function onCreate()
    screen_handle = crystal.ui.screen()
    
    local tabs = crystal.ui.tabview(screen_handle, {
        tab_labels = {"Favorites", "Search"},  -- Set at creation
    })
    
    -- Create tabs
    local tab_fav = crystal.ui.tab(tabs, 0)
    local tab_search = crystal.ui.tab(tabs, 1)
    
    -- Populate tab 0
    local fav_list = crystal.ui.list(tab_fav, {})
    crystal.ui.list_add(fav_list, "Favorite 1")
    
    -- Populate tab 1
    local search_input = crystal.ui.textarea(tab_search, {
        placeholder = "Search...",
    })
    
    -- Handle tab changes
    crystal.ui.on(tabs, "click", function(tab_index)
        if tab_index == 1 and search_input then
            -- Focus search field on first visit
            crystal.ui.set_focus(search_input)
        end
    end)
    
    return true
end
```

### 2.10 Common Patterns: State Machine

```lua
local state = "idle"
local current_item = nil

function onCreate()
    screen_handle = crystal.ui.screen()
    updateUI()
    return true
end

function updateUI()
    -- Clear old UI
    crystal.ui.delete(screen_handle)
    
    -- Rebuild based on state
    screen_handle = crystal.ui.screen()
    
    if state == "idle" then
        drawIdleUI()
    elseif state == "loading" then
        drawLoadingUI()
    elseif state == "detail" then
        drawDetailUI()
    end
end

function drawIdleUI()
    local list = crystal.ui.list(screen_handle, {})
    for i, item in ipairs(items) do
        crystal.ui.list_add(list, item.name)
    end
    crystal.ui.on(list, "click", function(idx)
        current_item = items[idx]
        state = "detail"
        updateUI()
    end)
end

function drawDetailUI()
    if not current_item then return end
    
    local title = crystal.ui.label(screen_handle, {
        text = current_item.name,
        font = "large",
    })
    
    local back_btn = crystal.ui.button(screen_handle, {
        text = "Back",
        on_click = function()
            state = "idle"
            updateUI()
        end,
    })
end

function onBack()
    if state ~= "idle" then
        state = "idle"
        updateUI()
        return true  -- Consumed
    end
    return false  -- Pass to next app
end
```

---

## 3. Native Tier: C++ Implementation

### 3.1 Minimal C++ App Structure

```cpp
// src/main.cpp
#include "crystal_app.h"
#include "crystal_ui.h"
#include "crystal_service.h"

class MyApp : public CrystalApp {
public:
    bool onCreate() override {
        // Build UI
        auto screen = crystal_ui_screen();
        auto label = crystal_ui_label(screen, {
            .text = "Hello, Native",
            .font = CRYSTAL_FONT_LARGE,
        });
        return true;
    }
    
    bool onResume() override {
        return true;
    }
    
    bool onPause() override {
        // Save state
        return true;
    }
    
    bool onDestroy() override {
        return true;
    }
    
    bool onBack() override {
        return false;  // Pass to next app
    }
};

// Entry point (called by host)
extern "C" CrystalApp* crystal_app_create() {
    return new MyApp();
}

extern "C" void crystal_app_destroy(CrystalApp* app) {
    delete app;
}
```

### 3.2 Native App with Service Handle

```cpp
// src/main.cpp
#include "crystal_app.h"
#include "my_service.h"

class BusApp : public CrystalApp {
private:
    my_service_handle_t service_handle_;
    ui_handle_t list_handle_;
    
public:
    bool onCreate() override {
        // Create per-app service instance
        service_handle_ = my_service_create(app_id());
        
        // Build UI
        auto screen = crystal_ui_screen();
        list_handle_ = crystal_ui_list(screen, {
            .item_height = 44,
        });
        
        // Fetch initial data
        my_service_request_routes(service_handle_, [this](const auto* routes) {
            for (const auto& route : *routes) {
                crystal_ui_list_add(list_handle_, route.name);
            }
        });
        
        return true;
    }
    
    bool onDestroy() override {
        // Destroy service; host verifies cleanup
        my_service_destroy(service_handle_);
        return true;
    }
};

extern "C" CrystalApp* crystal_app_create() {
    return new BusApp();
}
```

### 3.3 Native App with Worker Thread

```cpp
#include "crystal_app.h"
#include "esp_http_client.h"
#include "freertos/task.h"

class DataApp : public CrystalApp {
private:
    TaskHandle_t worker_task_;
    QueueHandle_t request_queue_;
    
    static void worker_task_entry(void* arg) {
        auto* self = (DataApp*)arg;
        self->workerLoop();
        vTaskDelete(NULL);
    }
    
    void workerLoop() {
        while (true) {
            // Wait for request
            request_t req;
            if (xQueueReceive(request_queue_, &req, portMAX_DELAY)) {
                // Do HTTP request
                esp_http_client_handle_t client = 
                    esp_http_client_init(&req.config);
                // ...
                
                // Post result back to LVGL task
                lv_async_call(deliver_result, result);
            }
        }
    }
    
public:
    bool onCreate() override {
        // Create worker task
        xTaskCreatePinnedToCore(
            worker_task_entry, "data_worker", 8192, this,
            5, &worker_task_, 0  // Core 0
        );
        request_queue_ = xQueueCreate(10, sizeof(request_t));
        return true;
    }
    
    bool onDestroy() override {
        // Cleanup: wait for task, delete queue
        if (worker_task_) {
            vTaskDelete(worker_task_);
            worker_task_ = NULL;
        }
        if (request_queue_) {
            vQueueDelete(request_queue_);
            request_queue_ = NULL;
        }
        return true;
    }
};
```

---

## 4. Declarative Tier: JSON UI

### 4.1 Minimal Declarative App (ui.json)

```json
{
  "screen": {
    "type": "container",
    "flex_direction": "column",
    "children": [
      {
        "type": "label",
        "text": "Welcome to Crystal OS",
        "font": "large",
        "align": "center"
      },
      {
        "type": "button",
        "text": "OK",
        "on_click": "navigate('home')"
      }
    ]
  }
}
```

### 4.2 Declarative App with Data Binding

```json
{
  "screen": {
    "type": "container",
    "flex_direction": "column",
    "children": [
      {
        "type": "label",
        "text": "{{crystal.service.time.now | format '%H:%M:%S'}}",
        "font": "large",
        "refresh_interval_ms": 1000
      },
      {
        "type": "label",
        "text": "Network: {{crystal.service.net.status.state}}",
        "font": "medium"
      },
      {
        "type": "button",
        "text": "Start Timer",
        "on_click": "crystal.service.timer.arm(300)"
      }
    ]
  }
}
```

### 4.3 Declarative App with Conditional Rendering

```json
{
  "screen": {
    "type": "container",
    "flex_direction": "column",
    "children": [
      {
        "type": "label",
        "text": "{{crystal.service.net.status.state}}",
        "visible": "{{crystal.service.net.status.state != 'disconnected'}}"
      },
      {
        "type": "label",
        "text": "Disconnected",
        "visible": "{{crystal.service.net.status.state == 'disconnected'}}"
      }
    ]
  }
}
```

---

## 5. ABI Implementation: Host Side (C/C++)

### 5.1 Handle Table Implementation

```cpp
// crystal_ui_internal.h
#include <unordered_map>
#include "lvgl.h"

class AppHandleTable {
private:
    std::unordered_map<uint32_t, lv_obj_t*> handles_;
    uint32_t next_handle_ = 1;
    
public:
    uint32_t allocate(lv_obj_t* obj) {
        uint32_t h = next_handle_++;
        handles_[h] = obj;
        return h;
    }
    
    lv_obj_t* get(uint32_t h) {
        auto it = handles_.find(h);
        if (it == handles_.end()) {
            return nullptr;
        }
        return it->second;
    }
    
    void free(uint32_t h) {
        handles_.erase(h);
    }
    
    void clear() {
        handles_.clear();
        next_handle_ = 1;
    }
};

// crystal_ui.c (ABI implementation)
static AppHandleTable* g_app_handles = nullptr;

uint32_t crystal_ui_screen(void) {
    lv_obj_t* screen = lv_obj_create(NULL);
    return g_app_handles->allocate(screen);
}

uint32_t crystal_ui_label(uint32_t parent_h, const crystal_ui_label_opts_t* opts) {
    lv_obj_t* parent = g_app_handles->get(parent_h);
    if (!parent) {
        return CRYSTAL_HANDLE_INVALID;
    }
    
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, opts->text);
    
    return g_app_handles->allocate(label);
}

void crystal_ui_delete(uint32_t h) {
    lv_obj_t* obj = g_app_handles->get(h);
    if (obj) {
        lv_obj_delete(obj);
        g_app_handles->free(h);
    }
}
```

### 5.2 Per-App Allocator (Sandbox)

```cpp
// script_allocator.h
class ScriptAllocator {
private:
    uint8_t* arena_;
    size_t arena_size_;
    size_t used_;
    
public:
    ScriptAllocator(size_t limit_bytes)
        : arena_size_(limit_bytes), used_(0) {
        arena_ = (uint8_t*)ps_malloc(limit_bytes);
    }
    
    ~ScriptAllocator() {
        free(arena_);
    }
    
    void* allocate(size_t size) {
        if (used_ + size > arena_size_) {
            return nullptr;  // Quota exceeded
        }
        void* ptr = arena_ + used_;
        used_ += size;
        return ptr;
    }
    
    void deallocate(void* ptr) {
        // Simple arena allocator doesn't really free
        // (in production, use a real malloc-like allocator)
    }
};

// Lua integration
static void* script_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    auto* alloc = (ScriptAllocator*)ud;
    if (nsize == 0) {
        alloc->deallocate(ptr);
        return nullptr;
    }
    void* mem = alloc->allocate(nsize);
    if (mem && ptr) {
        memcpy(mem, ptr, std::min(osize, nsize));
    }
    return mem;
}

// In app init:
auto allocator = new ScriptAllocator(manifest.memory_kb * 1024);
lua_setallocf(L, script_alloc, allocator);
```

### 5.3 Count Hook: Watchdog

```cpp
// script_watchdog.h
static uint64_t g_callback_start_us = 0;
static const uint64_t CALLBACK_BUDGET_US = 50000;  // 50ms

static void script_count_hook(lua_State *L, lua_Debug *ar) {
    uint64_t now_us = esp_timer_get_time();
    if (now_us - g_callback_start_us > CALLBACK_BUDGET_US) {
        luaL_error(L, "Timeout: callback exceeded 50ms budget");
    }
}

void script_start_callback(lua_State *L) {
    g_callback_start_us = esp_timer_get_time();
    lua_sethook(L, script_count_hook, LUA_MASKCOUNT, 10000);
}

void script_end_callback(lua_State *L) {
    lua_sethook(L, nullptr, 0, 0);
}
```

### 5.4 Async HTTP Implementation

```cpp
// crystal_http.c
typedef struct {
    uint32_t request_id;
    const char* app_id;
    crystal_http_callback_t callback;
    void* callback_ctx;
} http_request_t;

static QueueHandle_t http_queue_ = nullptr;
static TaskHandle_t http_worker_task_ = nullptr;
static uint32_t next_request_id_ = 1;

uint32_t crystal_service_http_fetch(
    const char* app_id,
    const char* url,
    const crystal_http_opts_t* opts,
    crystal_http_callback_t cb,
    void* ctx
) {
    // Validate permission
    if (!is_url_allowed(app_id, url)) {
        return CRYSTAL_HANDLE_INVALID;
    }
    
    // Queue request
    http_request_t req = {
        .request_id = next_request_id_++,
        .app_id = strdup(app_id),
        .callback = cb,
        .callback_ctx = ctx,
    };
    
    xQueueSend(http_queue_, &req, portMAX_DELAY);
    return req.request_id;
}

static void http_worker_loop(void* arg) {
    while (true) {
        http_request_t req;
        if (xQueueReceive(http_queue_, &req, portMAX_DELAY)) {
            // Execute HTTP request
            esp_http_client_config_t config = {
                .url = req.url,
                .timeout_ms = 10000,
            };
            esp_http_client_handle_t client = esp_http_client_init(&config);
            esp_http_client_perform(client);
            
            int status = esp_http_client_get_status_code(client);
            // ... read response body ...
            
            // Deliver callback
            lv_async_call([](void* ud) {
                // This runs in LVGL task
                auto* req = (http_request_t*)ud;
                req->callback(req->request_id, status, body, req->callback_ctx);
                free(req);
            }, &req);
            
            esp_http_client_cleanup(client);
        }
    }
}
```

---

## 6. Boot and Installation

### 6.1 Boot Reconciliation

```cpp
// crystal_boot.c
typedef struct {
    char id[64];
    uint16_t slot;
    bool enabled;
    crystal_app_origin_t origin;
} CrystalAppEntry;

static void reconcile_apps() {
    CrystalAppEntry entries[CRYSTAL_MAX_APPS];
    int entry_count = 0;
    
    // 1. Load builtin apps
    const CrystalAppManifest* builtins = get_builtin_manifests();
    for (int i = 0; builtins[i].id; i++) {
        entries[entry_count++] = {
            .id = builtins[i].id,
            .slot = i,
            .enabled = true,
            .origin = CRYSTAL_ORIGIN_BUILTIN,
        };
    }
    
    // 2. Scan /apps for package manifests
    DIR* dir = opendir("/littlefs/apps");
    struct dirent* ent;
    while ((ent = readdir(dir))) {
        char manifest_path[256];
        snprintf(manifest_path, sizeof(manifest_path), 
                 "/littlefs/apps/%s/app.json", ent->d_name);
        
        FILE* f = fopen(manifest_path, "r");
        if (f) {
            CrystalAppManifest* manifest = parse_manifest_json(f);
            fclose(f);
            
            CrystalAppEntry entry = {
                .id = manifest->id,
                .slot = get_next_free_slot(entries, entry_count),
                .enabled = true,
                .origin = CRYSTAL_ORIGIN_PACKAGE,
            };
            entries[entry_count++] = entry;
        }
    }
    
    // 3. Reconcile with NVS registry
    nvs_handle_t nvs = open_nvs_registry();
    for (int i = 0; i < entry_count; i++) {
        nvs_set_blob(nvs, entries[i].id, &entries[i], sizeof(entries[i]));
    }
    
    // 4. Sort by slot and install enabled apps
    qsort(entries, entry_count, sizeof(entries[0]), compare_slot);
    for (int i = 0; i < entry_count; i++) {
        if (entries[i].enabled) {
            installApp(&entries[i]);
        }
    }
}
```

### 6.2 Installation (Package Format)

```cpp
// crystal_install.c
static esp_err_t install_from_staging(void) {
    // 1. Parse manifest
    FILE* f = fopen("/littlefs/apps/.staging/app.json", "r");
    CrystalAppManifest* manifest = parse_manifest_json(f);
    fclose(f);
    
    // 2. Verify ABI
    if (manifest->abi > CRYSTAL_ABI_VERSION) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    // 3. Check admission control
    if (!check_admission_control(manifest)) {
        return ESP_ERR_NO_MEM;
    }
    
    // 4. Verify signature
    if (!verify_signature("/littlefs/apps/.staging/", manifest->sig_alg, 
                         manifest->signature, manifest->key_id)) {
        if (!developer_mode_enabled()) {
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    
    // 5. Atomic rename
    char from_path[256], to_path[256];
    snprintf(from_path, sizeof(from_path), "/littlefs/apps/.staging/");
    snprintf(to_path, sizeof(to_path), "/littlefs/apps/%s/", manifest->id);
    
    rename(from_path, to_path);
    
    // 6. Write registry
    CrystalAppEntry entry = {
        .id = manifest->id,
        .slot = get_next_free_slot(),
        .enabled = true,
        .origin = CRYSTAL_ORIGIN_PACKAGE,
    };
    nvs_set_blob(nvs, manifest->id, &entry, sizeof(entry));
    
    return ESP_OK;
}
```

### 6.3 Uninstall and Cleanup

```cpp
// crystal_uninstall.c
static esp_err_t uninstall_app(const char* app_id) {
    CrystalAppEntry entry;
    if (!nvs_get_blob(nvs, app_id, &entry, sizeof(entry))) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    
    if (entry.origin == CRYSTAL_ORIGIN_BUILTIN) {
        // Can't uninstall builtin; disable instead
        entry.enabled = false;
        nvs_set_blob(nvs, app_id, &entry, sizeof(entry));
        return ESP_OK;
    }
    
    // Package app: delete folder + cleanup NVS
    
    // 1. Delete app folder
    char app_path[256];
    snprintf(app_path, sizeof(app_path), "/littlefs/apps/%s/", app_id);
    recursive_delete(app_path);
    
    // 2. Verify cleanup (check for leaked resources)
    cleanup_report_t report;
    if (crystal_app_verify_cleanup(app_id, &report) != ESP_OK) {
        ESP_LOGW(TAG, "App cleanup warnings: %s", report.details);
        // Force-disable instead of full uninstall
        entry.enabled = false;
        nvs_set_blob(nvs, app_id, &entry, sizeof(entry));
        return ESP_OK;
    }
    
    // 3. Erase namespaced NVS keys
    nvs_iterator_t it = nvs_entry_find(NULL, NULL, NVS_TYPE_ANY);
    while (it != NULL) {
        nvs_entry_info_t entry_info;
        nvs_entry_info(it, &entry_info);
        
        char prefix[128];
        snprintf(prefix, sizeof(prefix), "%s:", app_id);
        if (strncmp(entry_info.key, prefix, strlen(prefix)) == 0) {
            nvs_erase_key(nvs, entry_info.key);
        }
        it = nvs_entry_next(it);
    }
    nvs_iterator_release(it);
    
    // 4. Delete registry row
    nvs_erase_key(nvs, app_id);
    
    return ESP_OK;
}

esp_err_t crystal_app_verify_cleanup(const char* app_id, cleanup_report_t* report) {
    report->leaked_tasks = 0;
    report->leaked_timers = 0;
    report->leaked_lvgl_objects = 0;
    report->leaked_heap_bytes = 0;
    
    // Scan FreeRTOS task list for app_id tag
    // Scan LVGL object tree for app_id tag
    // Scan allocator for app_id tag
    
    return (report->leaked_tasks + report->leaked_timers) > 0 
        ? ESP_ERR_INVALID_STATE 
        : ESP_OK;
}
```

---

## 7. Testing and Debugging

### 7.1 Unit Test Template (Script App)

```lua
-- test/test_main.lua
-- Use lunit or similar

local app = require("src.main")

function test_onCreate()
    local result = app.onCreate()
    assert(result == true, "onCreate should return true")
end

function test_onPause_saves_state()
    app.onCreate()
    -- Set some state
    crystal.state.set("test_key", 42)
    app.onPause()
    -- Verify state was flushed
    assert(crystal.state.get("test_key") == 42)
end

function test_onBack_consumed()
    app.onCreate()
    local result = app.onBack()
    assert(result == false, "App should not consume back initially")
end
```

### 7.2 Integration Test (Full Lifecycle)

```bash
#!/bin/bash
# test/test_integration.sh

APP_ID="com.example.clock3p"

# 1. Pack
crystal pack ./com.example.clock3p/
CAPP_FILE="${APP_ID}-1.0.0.capp"

# 2. Install
crystal install --usb "$CAPP_FILE"

# 3. Launch and verify no crashes
crystal run "$APP_ID"

# 4. Check logs for errors
crystal logs -f &
LOG_PID=$!
sleep 5
kill $LOG_PID

# 5. Uninstall
crystal uninstall "$APP_ID"

# 6. Verify cleanup
curl http://crystal.local/api/v1/apps | jq ".[] | select(.id == \"$APP_ID\")"
# Should be empty

echo "Integration test passed"
```

### 7.3 Memory Profiling

```cpp
// test/test_memory.cpp
#include "crystal_app.h"
#include "esp_heap_trace.h"

void test_memory_leak_on_destroy() {
    // Start heap trace
    heap_trace_start(HEAP_TRACE_LEAKS);
    
    // Create and destroy app
    CrystalApp* app = create_app();
    app->onCreate();
    app->onPause();
    app->onDestroy();
    delete app;
    
    // Check for leaks
    heap_trace_dump(stdout);
    heap_trace_stop();
}
```

---

## 8. Checklists for Code Review

### Phase 14 Code Checklist

Closed on 2026-11-11. The illustrative function names above are implemented
through the production package/resource APIs and `tools/test_app_platform_v2.py`.
See [closure notes](PHASE_14_IMPLEMENTATION.md) for the NVS and native ownership
adaptations. Host signing is tested. Phase 15 now implements package
verification against a configurable public-key store; the project's release
key still needs configuration.

- [x] LittleFS factory image generated; successful partition mounting confirmed on the board
- [x] Boot reconciliation tested with a hand-deleted folder on the host and a persisted package after device reboot
- [x] Development `.capp` round-trip: pack → install → uninstall; unsigned admission requires explicit Developer Mode
- [x] Admission control rejects when aggregate `memory_kb` exceeds capacity
- [x] Per-app typed handles and ownership registration/release for curated native task, timer, queue, allocation, and object resources
- [x] `crystal_app_verify_cleanup()` detects retained tasks/queues and prevents removal while resources remain
- [x] NVS namespace isolation: logical `<app_id>:key` entries and full owner ID checked within an app-owned blob
- [x] Uninstall erases the package state blob; reinstall starts without the previous formula
- [x] Calculator `.capp` installs, launches, and uninstalls cleanly over 10 board cycles
- [x] Zero tracked resource leaks after Calculator teardown; 100 launches also passed on the host

### Phase 15 Code Checklist

- [ ] HTTP API endpoints all return JSON with matching schema
- [ ] Web UI loads and serves static files over WiFi and USB (asset flashed; board HTTP check and USB transport remain)
- [ ] Pairing PIN: generated at enable, displayed on device screen, accepted in external panel (hardware check remains)
- [ ] Bearer token: exchanged once per PIN, validated on all requests (hardware check remains)
- [x] Ed25519 package verifier uses configurable trusted public keys and rejects unknown keys; release key is not configured
- [x] `crystal pack --sign` adds signature to `.capp`; host signature tests pass
- [x] Host `catalog-sign`, `catalog`, and `catalog-download` support the signed HTTPS catalog workflow; device browsing remains
- [ ] `crystal install --usb` streams to `/apps/.staging/` and verifies
- [ ] `crystal logs -f` tails app stderr in real-time
- [ ] `crystal run` packs + installs + launches + streams logs end-to-end
- [ ] Install progress bar: shown for packages > 1 MB, updates smoothly
- [ ] On-device Manage Apps and external panel agree after edits (no divergence)

### Phase 16 Code Checklist

- [ ] `DeclarativeApp` class inherits from `CrystalApp` and implements lifecycle
- [ ] `ui.json` parser: creates LVGL objects from JSON structure
- [ ] Data binding: `{{crystal.service.time.now}}` updates every 1s without callback overhead
- [ ] Event routing: button clicks → `on_click` actions (system or app-defined)
- [ ] All bundled app UIs expressible: Clock, Weather, Calculator can be declarative
- [ ] No ABI additions in this phase: ABI v1 frozen at end

### Phase 17 Code Checklist

- [ ] Lua 5.4 sandbox: `_ENV` doesn't include `os`, `io`, `package`, `require`, `load`, `loadstring`
- [ ] Count hook: 50ms deadline per callback, raises Lua error on overrun
- [ ] Allocator hook: per-app memory ceiling, allocation fails when limit exceeded
- [ ] Async `http.fetch`: returns request_id, callback fires in LVGL task
- [ ] Worker thread quota: second request queues when limit reached (or errors; per spec)
- [ ] `data/` quota enforcement: writes beyond `storage_kb` fail with error
- [ ] `crystal run` works: pack, install, launch, log stream, uninstall
- [ ] Bus app converted to script: resource cleanup verified, no leaked tasks
- [ ] Runaway loop: `while true do end` produces app death + toast, not watchdog reset
- [ ] HTTP request cleanup: request started before `onDestroy()` cancels cleanly

