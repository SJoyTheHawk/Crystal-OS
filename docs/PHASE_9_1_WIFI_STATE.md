# Phase 9.1 — WiFi state truth and the time-sync signal

## Purpose

Phase 9 shipped the radio, the scan, the credentials dialog, and the connect
path. All of it works: the device associates, authenticates, and gets a lease.
What it does not have is a reliable way of *telling you* any of that.

Five defects, one shared root cause. Every surface that displays WiFi state is
written only from the event stream, and nothing reads the HAL's actual state when
it first appears. A widget built after an event has already fired shows whatever
its constructor hardcoded, forever. The clock has the inverse problem: SNTP is
started once at boot, before any interface is up, and never hears that a lease
arrived.

This add-on makes every WiFi surface derive from HAL state at build time, adds
the one missing user action (forget), and turns the connect event into the
trigger for time sync.

Nothing about the radio, the retry policy, gesture ownership, or app lifecycle
changes. This is state-presentation and signal-wiring only.

## Defects

### 1. Quick tile reads "On / Not Connected" while connected

`build_quick_settings()` creates the tile with a literal:

```cpp
s_quick_wifi = tile(LV_SYMBOL_WIFI "\nWiFi\nOn\nNot Connected", ...);
if (hal().wifi != nullptr && hal().wifi->enabled()) {
    lv_obj_add_state(s_quick_wifi, LV_STATE_CHECKED);
}
```

The tile checks `enabled()` but never `connected()`, and never reads
`last_ssid()`. The correct label only ever arrives through
`crystal_shell_wifi_event(UI_EVT_WIFI_GOT_IP)`.

The quick panel is built lazily on first open. Connection completes about 2.5 s
into boot (`service_task` waits 1200 ms, then `start()`, then association and
DHCP). Any open after that point builds a tile whose constructor text has already
been contradicted, and no further event is coming — the association is stable, so
the stream is silent. The tile stays wrong until the next disconnect.

**Fix.** The tile's initial text must be computed the same way the event handler
computes it. Extract that formatting into one function used by both:

```cpp
static void wifi_tile_text(char *out, size_t size)
{
    IWifi *wifi = hal().wifi;
    if (wifi == nullptr || !wifi->enabled()) {
        strlcpy(out, LV_SYMBOL_WIFI "\nWiFi\nOff", size);
    } else if (wifi->connected()) {
        snprintf(out, size, LV_SYMBOL_WIFI "\nWiFi\n%.32s", wifi->last_ssid());
    } else {
        strlcpy(out, LV_SYMBOL_WIFI "\nWiFi\nOn\nNot Connected", size);
    }
}
```

`crystal_shell_wifi_event` then reduces to calling this and re-applying
`LV_STATE_CHECKED`, for every WiFi event rather than a per-event label. There are
currently four separate `snprintf` sites building this string; they should become
zero.

Note `connected()` asks `esp_wifi_sta_get_ap_info()`, which reports association,
not a lease. A device associated but still waiting on DHCP will read as connected
here. That is the right answer for a tile label and the wrong answer for "can I
fetch"; see [Deferred](#deferred).

### 2. Tick mark on the WiFi page is wrong twice over

```cpp
snprintf(text, sizeof(text), "%.4s%.32s  %d%.3s",
         strcmp(networks[i].ssid, s_wifi_connecting) == 0 ? "✓ " : "", ...);
```

Two independent bugs in one expression.

**The glyph.** `✓` is U+2713, three bytes in UTF-8. LVGL's built-in Montserrat
faces carry ASCII plus the FontAwesome subset in `lv_symbol_def.h`; U+2713 is in
neither, so it renders as a missing-glyph box. Use `LV_SYMBOL_OK` (`0xF00C`),
which is in the subset that is already linked. The `%.4s` precision then also
needs checking — `LV_SYMBOL_OK " "` is four bytes, so it happens to survive, but
the truncation is load-bearing by accident and should be explicit.

**The comparison.** `s_wifi_connecting` is the SSID of the most recent *attempt*.
It is written on `UI_EVT_WIFI_CONNECTING` and never cleared — not on
`ConnectFailed`, not on `Disconnected`. So after a failed attempt the tick sits
next to a network you are not on, and it is a tick, not a spinner, so it reads as
success. Meanwhile the genuinely connected network gets no tick if the connection
predates the page.

The tick means "connected". It must compare against the connected SSID:
`wifi->connected() ? wifi->last_ssid() : ""`. The blue row highlight at
`crystal_shell.cpp:1449` has the same comparison and the same fix.

`s_wifi_connecting` is still wanted, but only to drive an in-progress
affordance on the attempted row, distinct from the tick. It must be cleared on
both terminal events.

### 3. Pressing the connected network cannot forget it

Every row runs the same handler:

```cpp
lv_obj_add_event_cb(button, [](lv_event_t *e) {
    ...
    wifi_open_credentials(rows[index].ssid);
}, LV_EVENT_CLICKED, ...);
```

Tapping the network you are already on reopens the password dialog. There is no
way to forget a network from the UI at all, so a wrong password saved to NVS can
only be cleared by reflashing — and because `esp_wifi_set_storage(WIFI_STORAGE_FLASH)`
persists it, the bad entry is retried on every boot.

**Fix.** The row handler branches on whether the tapped SSID is the connected
one. If it is, open a confirm sheet — forget is destructive and one stray tap on
a list row should not drop the network. Confirming calls a new HAL method:

```cpp
virtual void forget() = 0;   // IWifi
```

`DeviceWifi::forget()` must, in order: stop the retry timer and zero `retries_`
so the disconnect below does not trigger a reconnect; clear `last_ssid_`;
`esp_wifi_disconnect()`; write an all-zero `wifi_config_t` via
`esp_wifi_set_config()` to erase the NVS copy; then `notify(Disconnected)`.

The zeroed-config write is the part that actually forgets. Calling only
`disconnect()` leaves the credentials in flash and `start()` reconnects on the
next boot.

Ordering matters: clearing `last_ssid_` before `esp_wifi_disconnect()` also
disarms the retry branch in the disconnect handler, which gates on
`last_ssid_[0] != 0`. Doing it the other way round races the event loop.

### 4. Connect does not fire a signal others can consume

`wifi_event()` in `crystal_core.cpp` maps HAL events onto `crystal_ui_post()`,
and `ui_task` dispatches them to exactly one consumer — `crystal_shell_wifi_event`
— plus two inline toasts. Anything else that wants to know the network came up
has nowhere to attach. The clock is the first such consumer and item 5 is its
symptom, but the weather app in Phase 9.5 is the second and will hit the same
wall.

**Fix.** Keep the UI event as-is; it is a UI concern. Publish a named signal on
IDF's default event loop for *service* consumers — the same mechanism
`DeviceWifi` already consumes `WIFI_EVENT` and `IP_EVENT` through, so no new
infrastructure and no hand-rolled callback array:

```cpp
// crystal_core.hpp
ESP_EVENT_DECLARE_BASE(CRYSTAL_NETWORK_EVENT);
enum { CRYSTAL_NETWORK_CONNECTED, CRYSTAL_NETWORK_DISCONNECTED };
```

```cpp
// crystal_core.cpp
ESP_EVENT_DEFINE_BASE(CRYSTAL_NETWORK_EVENT);
```

`wifi_event()` posts `CONNECTED` on `GotIp` and `DISCONNECTED` on `Disconnected`
/ `ConnectFailed`, before the UI post. `Connecting` and `ScanDone` are not
published; subscribers care about reachability, not progress. Subscribe with
`esp_event_handler_instance_register`, which returns a handle for symmetric
unregistration.

Two constraints to honour, both consequences of the loop being shared:

*Handlers run on the system event task, whose stack is 2304 bytes
(`CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE`).* That is a modest budget shared
with the WiFi and IP handlers. Handlers must dispatch work, not do it — no
blocking calls, no deep call trees, and no LVGL, since this is not the UI task.
Anything substantial posts to a task that has room.

*Publishing on a signal nobody must miss is not a guarantee this loop makes.*
`esp_event_post` copies the payload into a queue of 32
(`CONFIG_ESP_SYSTEM_EVENT_QUEUE_SIZE`) and can fail if that queue is full. Check
the return and log it. Handler invocation order across subscribers is
unspecified, so no subscriber may depend on another having run first.

### 5. Clock never syncs because SNTP starts before the network

```cpp
hal().wifi->set_event_callback(wifi_event, nullptr);
hal().wifi->start();
start_sntp();
```

`start_sntp()` runs immediately after `start()` returns. `start()` is
non-blocking — it arms `pending_connect_` and returns before
`WIFI_EVENT_STA_START` has even dispatched. So `esp_netif_sntp_init()` is called
with no interface up. Its first request goes nowhere.

`ESP_NETIF_SNTP_DEFAULT_CONFIG` sets `start = true` and
`server_from_dhcp = false`, so the operation is one-shot at init. There is no
retry on interface-up, and `sntp_synced` is never called, so `s_time_valid` stays
false and the RTC keeps whatever it had.

This was masked while the radio was broken: no path reached a lease, so the
absence of sync looked like a WiFi problem.

**Fix.** Subscribe to `CRYSTAL_NETWORK_CONNECTED` (item 4) and drive SNTP from
it. On the signal, call `esp_netif_sntp_start()` if init already happened,
otherwise init then start. `DISCONNECTED` is not handled — tearing SNTP down and
rebuilding it per transition is churn for no benefit.

Set `config.start = false` at init so init and start are separable, and init
once, lazily, on the first `CONNECTED`.

The signal fires on every reconnect, so re-sync after a long disconnect comes
free.

Guard against a redundant sync storm: `GotIp` also fires on DHCP renewal, which
on some APs is every few minutes. Track whether a sync has landed since the last
`CONNECTED` and skip the start if one is already in flight.

**The subscriber belongs in `crystal_core`, not in the Clock app.** This is the
one design decision here worth stating outright, because the obvious reading of
"the clock app listens for the signal" is a crash.

Apps in Crystal OS are destroyed on switch — `onDestroy()` runs and the object is
freed. A handler registered from `onCreate()` and not unregistered in
`onDestroy()` leaves the loop holding a pointer into freed memory, and the next
`CONNECTED` post calls through it. This is Android's leaked-receiver bug with the
garbage collector removed: there is no reachability to save you, just a corrupted
call. Even done correctly, time sync would then only work while the Clock app
happened to be resident, which is not what anyone means by "the clock syncs".

Time sync is a service that outlives every app, so it subscribes once from
`crystal_core_init()` and never unregisters. The Clock *app* keeps consuming
`UI_EVT_TIME_SYNCED` on the UI task exactly as it does now.

If an app does subscribe to a network signal later — the Phase 9.5 weather app is
the likely first — the rule is that it stores the
`esp_event_handler_instance_t` from registration and calls
`esp_event_handler_instance_unregister_with` in `onDestroy()`, unconditionally.
Symmetric with `onCreate()`, no exceptions, because the failure mode is a wild
jump rather than a leak and will not reproduce on the bench.

## Order of work

Items 4 and 5 are one change and must land together — the signal has no purpose
without a subscriber, and the SNTP fix has no trigger without the signal.

Items 1 and 2 are independent of everything else and are the two the user sees
first. Do them first; they are small.

Item 3 needs the item 2 comparison fix already in place, because "is this the
connected row" is the same predicate for both the tick and the branch. Doing 3
before 2 means writing that predicate against `s_wifi_connecting` and then
rewriting it.

## Verification

Per item, on hardware. The simulator has no radio.

1. Connect, wait for the toast, then open quick settings for the first time.
   Tile reads the SSID, not "Not Connected". Close and reopen — still correct.
2. Open the WiFi page while connected. Tick renders as a glyph, not a box, on
   the connected row only. Then fail an attempt against a wrong password and
   confirm no tick appears on the attempted row.
3. Forget the connected network, then reboot. The device must come up
   unconnected and the page must offer the password dialog again. This is the
   only test here that survives a reflash, so it is the one worth automating.
4. Covered by 5.
5. Boot with a saved network and a deliberately wrong RTC. Clock corrects within
   a few seconds of the connect toast. Then disconnect, wait, reconnect —
   it corrects again.

Item 3's NVS behaviour is the one thing that cannot be checked from the UI.
Confirm with `esp_wifi_get_config()` after `forget()` and assert `ssid[0] == 0`.

## Deferred

**Signal strength on the tile.** `wifi_ap_record_t.rssi` is available from
`esp_wifi_sta_get_ap_info()` and the quick panel has the reserved cells for it
(see PHASE_8_5_QUICK_PANEL.md). Not in this add-on — it needs a poll, and this
add-on adds no timers.

**Distinguishing associated from routable.** `connected()` reports association.
For a tile label that is fine. Phase 9.5's fetch path needs the stronger
condition and should ask the network observer's `up`, which is `GotIp`-derived
and therefore lease-backed. If a third consumer needs it, promote it to
`IWifi::has_ip()` rather than letting each caller invent its own test.

**Multiple saved networks.** `forget()` clears the single stored config because
that is all the HAL models — one `wifi_config_t`, one `last_ssid_`. A known-
networks list is a real feature with its own storage schema, not an extension of
this fix.
