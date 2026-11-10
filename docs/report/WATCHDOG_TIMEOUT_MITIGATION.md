# Task Watchdog Timeout Mitigation

## Problem Statement

The ESP32-S3's task watchdog monitors the IDLE tasks on both CPU cores. If an IDLE task doesn't run for `CONFIG_ESP_TASK_WDT_TIMEOUT_S` (default: 5 seconds), the watchdog fires and triggers a reset or panic.

In Crystal OS, the LVGL task can block for extended periods during:
- **Touch panel I2C reads** (GT911 driver)
- **Network operations** (if performed synchronously on the LVGL task)
- **Heavy rendering** (complex widget trees, large images)

When the LVGL task monopolizes CPU time, the IDLE task on the same core cannot feed the watchdog, causing timeouts.

## Current Mitigation (Short-term)

### Solution Implemented: Subscribe LVGL Task to Watchdog

The LVGL task is explicitly subscribed to the task watchdog using `esp_task_wdt_add()`. This allows the LVGL task to reset the watchdog on behalf of the IDLE task.

**Location:** `main/main.cpp` after `bsp_display_start()`

**Mechanism:**
1. Find the LVGL task by name (`"taskLVGL"`) using `xTaskGetHandle()`
2. Subscribe it to the watchdog with `esp_task_wdt_add(lvgl_task)`
3. Create an LVGL timer that calls `esp_task_wdt_reset()` every 1000ms

**Rationale:**
- Quick to implement
- Does not require modifying managed components
- LVGL already runs a timer loop, so adding a watchdog reset is lightweight
- The watchdog still catches true hangs (if LVGL stops processing timers)

**Limitations:**
- Does not reduce the blocking time of I2C operations
- IDLE task still doesn't run during long I2C reads, but the watchdog is satisfied
- If LVGL timers stop running (deadlock, infinite loop), the watchdog will fire

## Long-term Hardening (Option 3)

### Architectural Solution: Move Touch Polling Off the LVGL Task

For production hardening, touch input should be read on a separate task and queued to LVGL. This prevents blocking I2C operations from starving the IDLE task.

### Architecture

```
┌─────────────────────┐
│  Touch Reader Task  │  (Dedicated task, Core 1)
│  Priority: 3        │
└──────────┬──────────┘
           │ Read GT911 via I2C (blocking)
           ▼
     ┌──────────┐
     │ FreeRTOS │
     │  Queue   │
     └────┬─────┘
          │ Touch events (x, y, state)
          ▼
┌────────────────────┐
│   LVGL Task        │  (Core 0)
│   Priority: 4      │
└────────────────────┘
     │ Process events & render
     ▼
  Display
```

### Implementation Steps

#### 1. Create a Touch Reader Task

**File:** `components/crystal_hal/src/crystal_touch.c` (new)

```c
#include "esp_lcd_touch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#define TOUCH_TASK_STACK_SIZE 4096
#define TOUCH_TASK_PRIORITY 3
#define TOUCH_TASK_CORE 1
#define TOUCH_QUEUE_SIZE 8

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t strength;
    bool pressed;
} touch_event_t;

static QueueHandle_t s_touch_queue = NULL;
static esp_lcd_touch_handle_t s_touch_handle = NULL;

static void touch_reader_task(void *arg)
{
    const TickType_t poll_interval = pdMS_TO_TICKS(10); // 100 Hz polling
    uint16_t x = 0, y = 0, strength = 0;
    bool last_pressed = false;

    for (;;) {
        vTaskDelay(poll_interval);

        uint16_t touch_x, touch_y;
        uint16_t touch_strength;
        uint8_t touch_cnt = 0;

        // This blocking I2C call runs on Core 1, LVGL runs on Core 0
        esp_lcd_touch_read_data(s_touch_handle);
        bool currently_pressed = esp_lcd_touch_get_coordinates(
            s_touch_handle, &touch_x, &touch_y, &touch_strength, &touch_cnt, 1
        ) && touch_cnt > 0;

        // Only enqueue on state changes or position deltas > threshold
        bool state_changed = (currently_pressed != last_pressed);
        bool moved = currently_pressed && (
            abs(touch_x - x) > 5 || abs(touch_y - y) > 5
        );

        if (state_changed || moved) {
            touch_event_t evt = {
                .x = touch_x,
                .y = touch_y,
                .strength = touch_strength,
                .pressed = currently_pressed
            };
            xQueueSend(s_touch_queue, &evt, 0); // Drop if full
            x = touch_x;
            y = touch_y;
            strength = touch_strength;
            last_pressed = currently_pressed;
        }
    }
}

esp_err_t crystal_touch_init(esp_lcd_touch_handle_t touch_handle)
{
    if (s_touch_queue != NULL) return ESP_ERR_INVALID_STATE;
    s_touch_handle = touch_handle;
    s_touch_queue = xQueueCreate(TOUCH_QUEUE_SIZE, sizeof(touch_event_t));
    if (s_touch_queue == NULL) return ESP_ERR_NO_MEM;

    BaseType_t ret = xTaskCreatePinnedToCore(
        touch_reader_task,
        "touch_reader",
        TOUCH_TASK_STACK_SIZE,
        NULL,
        TOUCH_TASK_PRIORITY,
        NULL,
        TOUCH_TASK_CORE
    );
    return ret == pdPASS ? ESP_OK : ESP_FAIL;
}

QueueHandle_t crystal_touch_get_queue(void)
{
    return s_touch_queue;
}
```

#### 2. Create an LVGL Input Device Driver

The LVGL `indev` driver polls the queue instead of calling I2C directly.

**File:** `components/crystal_hal/src/crystal_touch_lvgl.c` (new)

```c
#include "lvgl.h"
#include "crystal_touch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static touch_event_t s_last_touch = {0};

static void touch_read_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    (void)indev_drv;
    QueueHandle_t queue = crystal_touch_get_queue();
    touch_event_t evt;

    // Drain all pending events, keep the most recent
    while (xQueueReceive(queue, &evt, 0) == pdTRUE) {
        s_last_touch = evt;
    }

    data->point.x = s_last_touch.x;
    data->point.y = s_last_touch.y;
    data->state = s_last_touch.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

lv_indev_t *crystal_touch_register_lvgl(void)
{
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    return lv_indev_drv_register(&indev_drv);
}
```

#### 3. Replace BSP Touch Input

In `main/main.cpp`, instead of using `bsp_display_get_input_dev()`, initialize the custom touch driver:

```cpp
// OLD:
// require_boot_step(phone->setTouchDevice(bsp_display_get_input_dev()), "...");

// NEW:
esp_lcd_touch_handle_t bsp_touch = bsp_display_get_input_dev(); // Still needed for initialization
require_boot_step(crystal_touch_init(bsp_touch) == ESP_OK, "Failed to start touch reader");
lv_indev_t *touch_indev = crystal_touch_register_lvgl();
require_boot_step(phone->setTouchDevice(touch_indev), "Failed to set touch device");
```

### Benefits

1. **No Watchdog Timeout**: I2C reads run on Core 1, LVGL runs on Core 0. The IDLE0 task (which feeds the watchdog) is never blocked by touch reads.
2. **Consistent Frame Rate**: Touch polling does not stall rendering. LVGL always has fresh input data available from the queue.
3. **Debouncing Control**: The touch task can implement advanced filtering (debounce, palm rejection, velocity smoothing) without impacting render latency.
4. **Power Management**: The touch task can enter light sleep when no touch is detected, while LVGL continues rendering animations.

### Tradeoffs

- **Input Latency**: Touch events now pass through a queue. At 100 Hz polling, this adds ~10ms latency. For a 60 FPS UI, this is acceptable (16.7ms per frame).
- **Memory**: Requires an additional 4 KB stack + queue overhead (~64 bytes).
- **Complexity**: Two tasks instead of one, requires synchronization primitives.

### Testing

1. **Touch Responsiveness**: Verify that taps, swipes, and scrolls feel identical to the current implementation.
2. **Watchdog Timeout**: Run the update check for 30+ seconds while swiping continuously. Confirm no watchdog resets.
3. **Frame Rate**: Use `lv_refr_get_fps_avg()` to measure FPS before and after. Should be identical or improved.
4. **Power Consumption**: Measure idle current draw. Should be identical (touch task sleeps most of the time).

## References

- Task watchdog documentation: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/wdts.html
- LVGL input devices: https://docs.lvgl.io/8.4/porting/indev.html
- GT911 driver: `managed_components/espressif__esp_lcd_touch_gt911/`

## Changelog

- **2026-10-11**: Created document describing short-term mitigation (subscribe LVGL task to watchdog) and long-term hardening (dedicated touch reader task).
