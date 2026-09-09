# Phase 11 — Settings and power (code guide)

The information architecture is `SETTINGS_PROPOSAL.md`. This document is how it
gets built: what already exists, what has to be added, the decisions the proposal
left open, and the traps in this tree that will bite the implementation.

Read `SETTINGS_PROPOSAL.md` §1-§4 first for what the rows are and why. Nothing
here re-argues that; where the two disagree, this document is wrong and should be
corrected.

Scope is the full proposal: five root categories, all must-ship and should-ship
rows. The build order in §12 is arranged so anything cut late is cut cleanly,
because the exit criteria land in the first three steps.

## 0. Decisions this phase settles

Seven things were open across `DESIGN.md`, `IMPLEMENTATION_PLAN.md`,
`CODE_GUIDE.md` and the proposal. They are decided here, and the other documents
are updated to match rather than left to drift. D1-D5 come from the proposal; D6
and D7 are the navigation work this phase absorbs.

**D1 — Timeouts always apply; Energy Saving only shortens them.**
`check_power_state()` currently evaluates Dim and Off only inside
`if (energy_saving_enabled())`, so with the toggle off the panel never dims.
`DESIGN.md` §8 describes full → dim → off as the normal screen lifecycle. The
design wins. Energy Saving becomes a modifier, not the switch that decides
whether the lifecycle exists.

This is a user-visible behaviour change on every device that updates: panels that
never dimmed now dim. That makes the shipped defaults a product decision, not an
implementation detail — see §4.

**D2 — The root categories are Network, Display & Power, Sound, Region & Time,
System.** This replaces `DESIGN.md` §8's Network / Power / General / System /
Manage Apps and the matching rows in §3's page inventory. Manage Apps moves to
Phase 13, which is where the registry work already sits. `DESIGN.md` §3 and §8
are edited as part of this phase, the same way Phase 8.5 superseded §6 rather
than quietly diverging from it.

**D3 — Settings is a shell-owned page, not a `CrystalApp`.** `DESIGN.md` §5.5
already requires it: not in the ring, no page dot, layer 5, dismisses to the card
underneath. That is the WiFi page's pattern, and Settings uses the same one (§2).

**D4 — Timezone entries carry full POSIX DST rules.** A picker that stores `EST5`
is right today and an hour wrong in March. Every catalog entry ships its
transition rules (§7).

**D5 — Dim Brightness is raw HAL units on the 0-95 scale, not a percentage of
it.** `dim_brightness()` compares `kDimBrightness = 20` directly against
`hal().brightness->get()`. Both scales get called "%" in the proposal and in the
quick panel; the stored value is whatever `IBrightness::set()` takes, and the
range is 5-50 on that scale.

**D6 — Crystal owns the bottom edge; Brookesia's gesture navigation is off.**
A swipe up from the bottom currently dismisses the *card underneath* an open
shell layer instead of the layer itself. Phase 11 fixes it (§2.1). The launcher
stays and remains the destination for a bottom swipe on a bare card — that part
of the current behaviour is correct and is preserved deliberately.

**D7 — There is one WiFi Networks page, and it lives under Network.** The page
`wifi_page_open()` builds today *becomes* Settings › Network › WiFi Networks. The
quick panel does not open a separate page; it deep-links into that subpage with
the Settings stack underneath it (§5, §2.1). Backing out of it lands on Network,
not on a card.

## 1. What already exists

Know which rows are UI over a working backend and which need service work, or the
estimate is wrong by a factor of three.

Working, needs only wiring:

| Thing | Where |
|---|---|
| `IBrightness` get/set, clamped to 95 | `crystal_hal.cpp:31` |
| Codec volume 0-100 | `crystal_hal_set_volume()` |
| Timer chime | `crystal_hal_timer_alarm()` |
| WiFi enable/scan/connect/forget, one saved station | `IWifi`, `crystal_hal.hpp:23` |
| The whole SSID list, credential dialog, forget confirm | `wifi_page_open()`, shell |
| Battery percent + charging, polled ≤30 s | `IPower::readBattery()` |
| Power state machine, ramp on the service task | `check_power_state()`, `ramp_brightness()` |
| Wake-touch swallow | `crystal_core_consume_wake_touch()` |
| Timezone load at boot, `setenv`+`tzset` | `crystal_time_init()`, `crystal_core.cpp:638` |
| Manual time to system clock *and* RTC | `crystal_time_set()` |
| SNTP on network-up, RTC writeback | `network_signal_handler()`, `sntp_synced()` |
| Weather lat/lon/city with range validation | `load_weather_location()` |
| Keyboard overlay with viewport rebinding | `crystal_keyboard_show()` |
| Restart | `esp_restart()`, already on the button hold |

Needs new code, in rough order of size:

- **Static IP.** `IWifi` has no address API at all. New HAL surface (§5).
- **System info.** Heap, PSRAM, uptime, reset reason, IDF version, MAC, chip id
  are all available from ESP-IDF and none are behind a Crystal interface (§6).
- **Timezone catalog.** No list exists; only the stored string (§7).
- **Energy Saving effects.** The flag is stored and read. The CPU cap,
  `WIFI_PS_MAX_MODEM`, and brightness ceiling are not implemented (§4).
- **Power values from NVS.** Three compile-time constants become stored,
  validated settings (§4).
- **Time format.** `update_clock()` hardcodes 12-hour (§7).
- **Runtime location change.** Weather caches location in service globals; there
  is no API to replace it and refresh (§8).
- **SNTP policy.** Sync is unconditional on network-up, no enabled flag, no
  last-sync value (§7).

## 2. The page: one pattern, borrowed from WiFi

### Where it is opened from

Two entry points exist in the quick panel today, both in
`components/crystal_shell/src/crystal_shell.cpp`:

- **The gear tile** (line 311) closes the panel and calls `quick_show_message()`,
  which shows the toast `"Settings coming in Phase 11"` (line 210). Replace the
  toast with `settings_open()`. Keep the panel-close animation exactly as it is —
  the gear already runs the same dismissal the other tiles use, and the page should
  open after the panel is gone, not behind it. The `message[]` string and the
  `quick_show_message(nullptr)` call for the gear both go away; check whether
  `quick_show_message()` still has other callers before deleting it.
- **The WiFi tile's long-press** (line 284) currently does
  `close_quick_settings(wifi_page_open)`. It becomes
  `close_quick_settings(settings_open_at_wifi)` — the deep link in §5. Note the
  existing pattern: `close_quick_settings()` takes the follow-up as a callback so
  it runs after the close animation finishes. Use it rather than opening the page
  first.

There is no third entry point, and Phase 11 does not add one. Settings is not in
the ring and has no launcher icon (D3).

Settings is built like `wifi_page_open()` — `lv_obj_create(lv_layer_top())`, sized
and positioned from `active_app_area()`, `crystal_shell_set_settings_open(true)`.
Do not parent to `lv_scr_act()` and do not use `area.x1/y1` as a child offset;
`active_app_area()` is display coordinates, which is the Phase 9.5 trap.

Three rules the WiFi page already demonstrates, all of which apply per subpage:

**Every push and pop calls `crystal_shell_front_layer_changed()`.** The keyboard
holds pointers into the layer that owned its field. Without the call, a keyboard
left open on the manual-IP form is orphaned behind the next subpage and
`shell_consume_back()` closes it instead of the page.

**One teardown path, and it clears every cached pointer.** `lv_obj_del()` frees
children, so a stale `s_*` pointer still passes `!= nullptr` at every use site.
`wifi_page_close()` is the model.

**Back is a stack now, not an if-chain.** `shell_consume_back()` handles keyboard
→ dialog → wifi page → quick panel as a flat sequence because each was unique.
Settings has depth, and `DESIGN.md` §5.5 requires two-level back: pop the subpage
if inside one, dismiss the override only from the root.

```cpp
// One entry per pushed system page. Bounded: the deepest path in the proposal is
// root -> System -> About -> Legal, so 4 is enough, and a fixed array avoids
// an allocation on a UI path. The WiFi page is an entry in this stack, not a
// parallel mechanism (D7).
constexpr size_t kSystemPageDepthMax = 4;
lv_obj_t *s_system_page_stack[kSystemPageDepthMax];
size_t s_system_page_depth = 0;
```

`s_settings_open` stops being a bool that two different pages both set and clear.
It becomes `s_system_page_depth > 0`. That kills the bug where the WiFi page's
close clears the flag while the Settings root is still up — the flag is now
derived from the stack, so it cannot disagree with what is on screen.

`shell_consume_back()` gains one clause, above the quick-panel one and below the
keyboard and modal ones, and the separate `s_wifi_page` clause goes away:

```cpp
if (s_system_page_depth > 1) { system_page_pop(); return true; }   // subpage
if (s_system_page_depth == 1) { system_page_close(); return true; } // root -> card
```

Getting this wrong has a specific symptom worth naming: Back from inside Network
drops the user onto a card instead of the Settings root. That is the failure
`DESIGN.md` §5.5 calls out by name, and it is the one to test first.

### 2.1 The bottom edge: fixing the swipe that dismisses the wrong thing

This is a real bug, not a design limitation, and it predates Settings — the WiFi
page has had it since Phase 8. Swipe up from the bottom with the WiFi page open
and the page stays while the *card behind it* is dismissed to the launcher.

**Why.** Two independent things listen to the same gesture object, and only one of
them knows the shell has a front layer.

Brookesia's manager registers `onGestureNavigationReleaseEventCallback`, which on
`GESTURE_DIR_UP` calls `sendNavigateEvent(HOME)`. `processNavigationEvent()`
handles HOME by calling `processAppPause()`, `processHomeScreenChange(MAIN)` and
`resetActiveApp()` — with **no hook consulted anywhere**. That is the asymmetry:
BACK routes through `active_app->back()`, which is `CrystalApp::back()`, which
calls `s_shell_back_hook()`. HOME has no equivalent. So Back correctly closes the
WiFi page and a bottom swipe correctly ignores it.

The shell's arbiter does classify the gesture — with a front layer open it locks
`CrystalGestureOwner::App` — but ownership is advisory between *Crystal's* own
handlers. Brookesia's callback does not check `s_gesture_owner`, and the shell
registers its callbacks in `crystal_shell_init()`, which runs after
`phone->begin()`, so Brookesia's handler fires first regardless. Ordering cannot
fix this and neither can the arbiter as it stands.

**The fix: Crystal takes the bottom edge, the same way it already took the side
edges.** Card switching works because Crystal owns left/right — the stylesheet
ships `enable_gesture_navigation_back = 0`, so Brookesia never competes for them.
Do the same for the bottom.

Per-app data is the lever. `processGestureScreenChange()` computes
`enable_gesture_navigation` from `app_data->flags.enable_navigation_gesture` at
every screen change, and `enable_gesture_navigation_home` is gated on it. Clear
that flag and Brookesia raises neither HOME nor recents from a gesture.

The flag lives in `_init_data`, which is **private** in `ESP_Brookesia_PhoneApp`
with only a const `getActiveData()`. So it cannot be cleared after construction —
it has to be passed in. `CrystalApp`'s current 3-arg
`ESP_Brookesia_PhoneApp(name, launcher_icon, true)` hardcodes the default macro.
Switch to the 2-arg `(core_data, phone_data)` constructor
(`esp_brookesia_phone_app.hpp:33`) and hand it modified data. No upstream patch:

```cpp
namespace {
// ESP_BROOKESIA_PHONE_APP_DATA_DEFAULT() is a brace initialiser, so the flag
// cannot be overridden inline. Build it, then clear the one bit.
ESP_Brookesia_PhoneAppData_t crystal_phone_app_data(const void *launcher_icon)
{
    ESP_Brookesia_PhoneAppData_t data =
        ESP_BROOKESIA_PHONE_APP_DATA_DEFAULT(launcher_icon, true, false);
    // Crystal's arbiter owns the bottom edge (D6). Leaving this set lets
    // Brookesia raise HOME behind an open shell layer.
    data.flags.enable_navigation_gesture = 0;
    return data;
}
}  // namespace

CrystalApp::CrystalApp(const char *name, const void *launcher_icon)
    : ESP_Brookesia_PhoneApp(
          ESP_BROOKESIA_CORE_APP_DATA_DEFAULT(name, launcher_icon, true),
          crystal_phone_app_data(launcher_icon)),
      app_name_(name != nullptr ? name : "<unnamed>"), state_(name)
{
}
```

Two things make this safe, both verified against upstream rather than assumed:

**The data is copied, not referenced.** `ESP_Brookesia_CoreApp` stores
`_core_init_data(data)` (`esp_brookesia_core_app.cpp:29`) and
`ESP_Brookesia_PhoneApp` stores `_init_data(phone_data)`
(`esp_brookesia_phone_app.cpp:18`) — both by value, into members declared as
plain structs. So returning a temporary from `crystal_phone_app_data()` and
binding it to a `const &` parameter is fine; the copy happens before the
temporary dies. Nothing here would be safe if either base held a pointer, so it
is worth knowing which it is.

**The macro arguments are the ones the 3-arg path already used.** It expands
`ESP_BROOKESIA_CORE_APP_DATA_DEFAULT(name, launcher_icon, use_default_screen)`
(`esp_brookesia_core_app.cpp:48`) and
`ESP_BROOKESIA_PHONE_APP_DATA_DEFAULT(launcher_icon, true, false)`
(`esp_brookesia_phone_app.cpp:33`). The code above passes exactly those, with
`use_default_screen` fixed at `true` because `CrystalApp` always passed `true`.
Change nothing else in either argument list: `status_icon` doubling as
`launcher_icon` is what drives `image_num`, and `use_status_bar = true` /
`use_navigation_bar = false` are what give Crystal apps a status bar and no nav
bar.

`name` remains a `const char *` held by pointer in both the old and new path
(`getName()` returns `_core_active_data.name`), so the lifetime requirement on
the caller is unchanged — it was already "must outlive the app", and every
`CrystalApp` subclass passes a literal.

**The flag reaches the manager.** `beginExtra()` does `_active_data = _init_data`
(`esp_brookesia_phone_app.cpp:74`) at install, then applies its own consistency
fixes; `processGestureScreenChange()` reads `getActiveData()`. So a bit cleared in
`_init_data` by the constructor is the bit the manager sees. Note that
`beginExtra()` already clears `enable_navigation_gesture` itself when no gesture
object exists — clearing it deliberately is the same operation upstream performs,
not a new kind of change.

If the constructor swap turns out to be awkward, do **not** substitute a
`const_cast` on `getActiveData()`. It would appear to work — install happens once
at boot and nothing rewrites `_active_data` afterwards — which is exactly the
problem: it depends on install-once timing that nothing enforces, and
`delExtra()` zeroes the struct, so anything that ever uninstalls and reinstalls an
app silently loses the flag. The constructor is the supported route and costs one
helper function.

**Then handle the gesture.** The arbiter gains a bottom band and a real owner
instead of falling through to `App`:

```cpp
constexpr lv_coord_t kBottomBand = 24;  // matches kEdgeBand
```

In `on_gesture_pressing()`, before the `horizontal_edge` test, a swipe starting
inside `kBottomBand` of the bottom going up locks
`CrystalGestureOwner::Navigation`. Two cases must not claim it: the keyboard
(which already claims everything) and an open quick panel (an upward drag there
dismisses the panel — that path exists and stays).

In `on_gesture_release()`, `Navigation` resolves in order:

```cpp
// One swipe dismisses one shell layer, top down -- the same rule as Back,
// because they are the same question asked with a different gesture.
if (owner == CrystalGestureOwner::Navigation) {
    if (shell_consume_back()) return;
    // Nothing of the shell's was on top: the card is the front layer, so the
    // swipe means what Brookesia meant by it. The launcher stays and is the
    // right destination here.
    if (s_phone != nullptr) (void)s_phone->sendNavigateEvent(
        ESP_BROOKESIA_CORE_NAVIGATE_TYPE_HOME);
    return;
}
```

`sendNavigateEvent()` is public on `ESP_Brookesia_Core`
(`esp_brookesia_core.hpp:49`), so the launcher path is Brookesia's own code,
reached deliberately instead of by accident. Reusing `shell_consume_back()` rather
than writing a second dismissal chain is the point: keyboard, modal, system page
and quick panel already have a documented precedence, and one swipe now peels one
layer exactly as one Back does.

Require a travel threshold on the release — a tap in the bottom 24px must not
dismiss anything. **Do not use half the screen height.** This section originally
said to, matching card switching's `hor_res / 2`; that shipped and the gesture was
unusable. Half the screen is right for card switching, where the drag animates a
card across and the commit point should be the midpoint. The bottom swipe animates
nothing — it is a flick, and demanding 240px of travel from a 24px band meant
releases were silently dropped, which read as "the pill does nothing".

`kHomeSwipeTravel = 80` instead. The floor is Brookesia's `direction_vertical = 50`
from the 480x480 stylesheet: below that the direction never resolves as UP, so the
arbiter would claim gestures that cannot commit.

**What this costs.** `enable_gesture_show_mask_bottom_edge` and
`enable_gesture_show_bottom_indicator_bar` are both computed from
`enable_gesture_navigation`, so clearing the flag also removes Brookesia's bottom
indicator pill on app screens. **This was missed, and the shell now draws its own
pill** — `s_home_pill` in `init_indicator_overlay()`, 172x4px at 30% white,
`kHomePillInset` above the bottom edge, hidden while the keyboard is open.

Two corrections to what this section originally claimed. The loss was **app
screens only**: the `MAIN` branch of `processGestureScreenChange()` derives
`enable_gesture_navigation` from the navigation bar being `HIDE`, which is still
true, so the launcher kept its pill; only the `APP` branch reads the app flag.
And Brookesia's pill was never a resting hint — `size_min` is `RECT(0, 10)`, zero
width, so it existed only while a drag stretched it. Crystal's rests visible,
which is a deliberate change rather than a restoration. Do not
set `navigation_bar_visual_mode = SHOW_FLEX` to get the hint back: it disables
gesture HOME by a side effect and puts a real nav bar on screen, which is not the
Crystal layout.

**Test it on a card with no shell layer open first.** If a bare bottom swipe stops
reaching the launcher, the arbiter is claiming the gesture and not forwarding it,
and every other test in this section will be misleading.

## 3. Storage keys

Everything shell-level goes through `hal().storage`, which is the `crystal` NVS
namespace (`crystal_hal.cpp:33`). **Not `CrystalState`** — that prefixes per app
and is app data only.

Existing keys. Settings reads and writes these, and must not invent parallel ones:

| Key | Type | Default | Written by today |
|---|---|---|---|
| `brightness` | `uint8_t` | 95 | quick panel slider |
| `volume` | `uint8_t` | 85 | quick panel slider |
| `power.saving` | `uint8_t` 0/1 | 0 | quick panel Energy tile |
| `timezone` | string | `"HKT-8"` | first-boot default |
| `wifi.enabled` | `uint8_t` | 1 | WiFi adapter |
| `weather.lat` | `double` | — | location resolver |
| `weather.lon` | `double` | — | location resolver |
| `weather.city` | string | — | location resolver |

New keys. Every one has a type, a default, a valid range, and clamp-on-read
behaviour — an out-of-range value from a downgrade or a corrupt write must not
brick the screen:

| Key | Type | Default | Valid | Notes |
|---|---|---|---|---|
| `power.dim_s` | `uint16_t` | 30 | 0, 15, 30, 60, 300 | 0 = never |
| `power.off_s` | `uint16_t` | 60 | 0, 60, 120, 300, 900 | 0 = never; must exceed `dim_s` |
| `power.dim_level` | `uint8_t` | 20 | 5-50 | HAL units, see D5 |
| `net.dhcp` | `uint8_t` 0/1 | 1 | — | 0 = static |
| `net.ip` `net.mask` `net.gw` `net.dns1` `net.dns2` | `uint32_t` | 0 | — | network byte order; `dns2` optional |
| `time.auto` | `uint8_t` 0/1 | 1 | — | SNTP enabled |
| `time.last_sync` | `int32_t` | 0 | — | epoch of last success |
| `time.format24` | `uint8_t` 0/1 | 0 | — | 0 = 12-hour, matching today |
| `loc.auto` | `uint8_t` 0/1 | 1 | — | 0 = manual coordinates |
| `sound.alerts` | `uint8_t` 0/1 | 1 | — | timer/alarm chime |
| `dev.name` | string | `"crystal"` | 1-32 chars | hostname charset only |

Two constraints are relational, so validate them together on commit, not per
field: `off_s > dim_s` when both are non-zero, and the manual IP set is all-or-
nothing.

Clamp on read, in one place per value. A helper beats five copies of the same
bounds check:

```cpp
uint16_t stored_u16(const char *key, uint16_t fallback,
                    const uint16_t *allowed, size_t count)
{
    uint16_t value = 0; size_t length = sizeof(value);
    if (hal().storage == nullptr ||
        !hal().storage->get(key, &value, &length) || length != sizeof(value)) {
        return fallback;
    }
    for (size_t i = 0; i < count; ++i) if (value == allowed[i]) return value;
    return fallback;   // unknown value from a downgrade: fall back, do not honour
}
```

## 4. Power: D1, and the defaults it makes load-bearing

`check_power_state()` loses its energy-saving gate and reads stored values:

```cpp
void check_power_state(lv_timer_t *)
{
    if (s_display == nullptr || s_service_task == nullptr) return;
    const uint32_t inactive_ms = lv_disp_get_inactive_time(s_display);

    uint32_t dim_ms = power_dim_seconds() * 1000u;   // 0 = never
    uint32_t off_ms = power_off_seconds() * 1000u;
    if (energy_saving_enabled()) {                    // modifier, not gate
        if (dim_ms != 0) dim_ms = dim_ms / 2;
        if (off_ms != 0) off_ms = off_ms / 2;
    }

    PowerState requested = PowerState::Full;
    if (off_ms != 0 && inactive_ms >= off_ms)      requested = PowerState::Off;
    else if (dim_ms != 0 && inactive_ms >= dim_ms) requested = PowerState::Dim;

    if (requested != s_power_state) {
        s_power_state = requested;
        xTaskNotify(s_service_task, static_cast<uint32_t>(requested),
                    eSetValueWithOverwrite);
    }
}
```

Order matters: test Off before Dim, since both thresholds are true once the screen
has been idle long enough, and `0 = never` has to be checked before the
comparison rather than relying on `inactive_ms >= 0` being false.

Halving is one defensible reading of "shorter timeouts" and it is arbitrary. Pick
it, write it down, and keep it out of the UI — the panel shows the user's chosen
value, not the effective one, or the row appears to change itself.

Three things about this code that are already true and easy to break:

- **Transitions execute on the service task, never inline.** `ramp_brightness()`
  sleeps 25 ms per step; on the LVGL task that is a stall and eventually the 5 s
  watchdog. The `xTaskNotify` is the whole point.
- **`dim_brightness()` refuses to raise brightness.** A user sitting at 10% must
  not get brighter when the screen dims. Keep the `>` comparison when the target
  becomes a stored value.
- **The wake touch is handled.** `crystal_core_consume_wake_touch()` exists and
  the arbiter calls it on touch-down. Do not add a second swallow path in
  Settings. Related trap, already documented: LVGL stamps activity only on
  `PRESSED`, so a swallowed wake touch does not reset the idle timer by itself.

### Energy Saving's promised effects

The flag is stored and read; none of these are implemented. All four belong in one
apply function on the service task, called at boot and whenever the flag changes,
so the quick-panel tile and the Settings row cannot diverge:

```cpp
void power_saving_apply(bool on)
{
    // min MUST equal max. A lower min enables dynamic frequency scaling, and
    // nothing holds an ESP_PM_CPU_FREQ_MAX lock, so the UI renders at min.
    const int freq = on ? 80 : 240;
    esp_pm_config_t pm = {
        .max_freq_mhz = freq,
        .min_freq_mhz = freq,
        .light_sleep_enable = false,   // never true in v1: RGB panel is DMA scan-out
    };
    (void)esp_pm_configure(&pm);

    if (hal().wifi != nullptr) hal().wifi->set_power_save(on);   // new, see 5

    const uint8_t ceiling = on ? kSavingBrightnessMax : kFullBrightness;
    if (hal().brightness != nullptr && hal().brightness->get() > ceiling) {
        ramp_brightness(ceiling);
    }
}
```

`light_sleep_enable = false` is not a default to revisit. `CONFIG_PM_ENABLE=y` is
already set in `sdkconfig.defaults`; automatic light sleep blanks or tears this
panel.

`min_freq_mhz == max_freq_mhz` is also not a default to revisit. An earlier
revision of this snippet pinned `min_freq_mhz = 80` with `max_freq_mhz = 240`
when saving was off. That shipped and caused the Phase 11 performance
regression: with `CONFIG_PM_ENABLE=y`, nothing had ever called
`esp_pm_configure()` before, so the SoC sat at `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ`
= 240 and DFS was inert. Giving it a real range switched DFS on, and because no
task in the tree takes an `ESP_PM_CPU_FREQ_MAX` lock, LVGL rendered at 80MHz —
stuttering animations, slow app loads, and an idle CPU figure several times its
old value. Energy Saving lowers both ends together; it does not widen the range.

The brightness ceiling clamps the current value but must not overwrite the stored
`brightness` — turning Energy Saving off restores what the user picked. That means
the ceiling is applied at the HAL call, not by rewriting NVS.

`WIFI_PS_MAX_MODEM` raises WiFi latency, which is fine for a 30-minute weather
poll and would not be for anything interactive. Worth knowing when the first
latency-sensitive network feature lands.

### The defaults are the product decision

D1 means an updated device dims where it did not before. Defaults above are 30 s
dim / 60 s off, matching today's constants, so behaviour matches what the code
always intended. If a wall-mounted always-on display is a supported deployment,
the honest default is `off_s = 0` with dim retained, and that is a call to make
before shipping rather than after the first complaint.

## 5. Network: the HAL additions

`IWifi` is `start`/`scan`/`connect`/`forget` plus queries. Everything the Network
page needs beyond the SSID list is missing. Add it to the interface — do not reach
for `esp_netif_*` or `esp_wifi_*` from the shell, or the simulator stops building
and the HAL boundary that makes it viable is gone.

```cpp
struct IWifi {
    // ... existing members unchanged ...

    struct IpConfig {
        bool dhcp;
        uint32_t ip, mask, gateway, dns1, dns2;   // network byte order, 0 = unset
    };

    // Live values from the interface. False when down: callers must render
    // "Not connected" rather than a stale or zeroed address.
    virtual bool ip_config(IpConfig *out) const = 0;
    // Applies and persists. Validated by the caller; the adapter assumes a
    // complete config. Reconnects, so it is not safe to call mid-scan.
    virtual bool set_ip_config(const IpConfig &config) = 0;

    virtual bool mac(uint8_t out[6]) const = 0;
    // Live RSSI for the connected AP, distinct from the per-network scan value.
    virtual bool rssi(int8_t *out) const = 0;
    virtual void set_power_save(bool enabled) = 0;
    virtual bool set_hostname(const char *name) = 0;
};
```

The static-IP apply path on the device side, and the ordering that matters:

```cpp
bool WifiAdapter::set_ip_config(const IWifi::IpConfig &config)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == nullptr) return false;

    if (config.dhcp) {
        (void)esp_netif_dhcpc_start(netif);      // already running: harmless
        return true;
    }

    // DHCP client must stop before the address is set, or the lease overwrites it.
    (void)esp_netif_dhcpc_stop(netif);
    esp_netif_ip_info_t info = {};
    info.ip.addr = config.ip; info.netmask.addr = config.mask; info.gw.addr = config.gateway;
    if (esp_netif_set_ip_info(netif, &info) != ESP_OK) return false;

    esp_netif_dns_info_t dns = {};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = config.dns1;
    (void)esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
    if (config.dns2 != 0) {
        dns.ip.u_addr.ip4.addr = config.dns2;
        (void)esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns);
    }
    return true;
}
```

**Static DNS is not optional.** Stopping the DHCP client discards the DNS servers
it supplied. Commit a static config with no DNS and the device keeps its address,
loses name resolution, and Weather fails in `getaddrinfo()` while the WiFi icon
stays lit — a failure that looks like a broken app rather than a bad setting. So
`dns1` is a required field on the form.

**Validate everything before touching the interface.** `DESIGN.md` §11 lists
"static IP misconfigured → device must stay reachable enough to fix itself", and
this is that requirement. Validate on commit, not per keystroke:

- all four required fields parse as dotted quads
- mask is contiguous (`~mask + 1` is a power of two)
- `ip & mask == gateway & mask` — gateway on-subnet
- ip is neither the network nor the broadcast address

A half-written config must never reach `set_ip_config()`. Keep the fields disabled
while DHCP is selected, and commit atomically.

Recovery matters because the device configures its own network from its own
screen. The screen is local, so a bad static config cannot lock the user out of
the UI — Settings stays reachable and the row can be set back to Automatic. That
is the property to preserve: never apply a config the form has not validated, and
never leave the page in a state where DHCP cannot be re-selected.

### The WiFi page becomes Settings › Network › WiFi Networks (D7)

Do not build a second SSID list, and do not leave the existing one standing beside
Settings as a page with its own rules. `wifi_page_open()` already handles scan,
credentials, connect, connected-row state and forget, and `DESIGN.md` §6 says
there is one SSID list in the system. It becomes a subpage — the same list, the
same code, reached through the stack from §2.

What changes about it:

- It pushes and pops through `system_page_push()` / `system_page_pop()` instead of
  creating and deleting itself independently. Its own `crystal_shell_set_settings_open()`
  calls go away; depth is the flag now.
- Its "Back" button becomes the standard subpage header from §10, and it pops one
  level instead of closing outright.
- `wifi_page_close()`'s cached-pointer clearing survives verbatim. It is still the
  only teardown path for that tree, and it is still the model the other subpages
  copy.

**The quick panel deep-links; it does not open a page.** Long-pressing the WiFi
tile must land on this subpage with Network and the Settings root beneath it, so
Back and a bottom swipe walk out through Network to the root and only then to the
card. Opening the page bare would put the user somewhere with no way back into
Settings, which is exactly the isolated-overlay behaviour this phase removes.

```cpp
// Build the stack the user would have built by hand, then show the leaf. The
// intermediate pages are constructed, not faked -- popping to Network has to
// find a real Network page there.
void settings_open_at_wifi()
{
    close_quick_settings(nullptr);
    settings_open();                 // root
    settings_push_network();         // Network
    settings_push_wifi_networks();    // WiFi Networks
}
```

Building three pages on one tap is more work than the 80ms card budget allows for
a single push, so measure it. If it is visibly slow, construct the leaf and mark
the intermediates lazily — but the stack entries must exist either way, because
`system_page_pop()` has to have something to return to. Do not solve it by making
the WiFi page a root-level page again.

## 6. System info: one interface, or the simulator dies

About and Device Status need heap, PSRAM, uptime, reset reason, IDF version, chip
id and flash size. All are one ESP-IDF call each, which is exactly why they will
get called directly from the page. Don't. The same rule that applies to static IP
applies here — `sim/crystal_hal_mock.cpp` is a plain host build with no ESP-IDF.

```cpp
struct ISystemInfo {
    virtual ~ISystemInfo() = default;
    virtual uint32_t free_heap() const = 0;
    virtual uint32_t free_psram() const = 0;
    virtual uint32_t uptime_seconds() const = 0;
    virtual const char *reset_reason() const = 0;   // static string
    virtual const char *idf_version() const = 0;
    virtual const char *app_version() const = 0;
    virtual bool chip_id(uint8_t out[6]) const = 0;
    virtual bool storage_bytes(uint32_t *used, uint32_t *total) const = 0;
};
```

Add `ISystemInfo *system_info` to `CrystalHal`. `app_version()` comes from
`esp_app_get_description()->version`, which needs the project version wired in
CMake to be meaningful rather than `1`.

**The simulator mock is already behind.** `sim/crystal_hal_mock.cpp` is 88 lines
and predates `has_ip()` and `IPower` — it does not compile against the current
`crystal_hal.hpp`. Phase 11 adds two interfaces and six `IWifi` methods on top of
that. Either fix the mock as part of this phase or accept that the simulator is
gone; leaving it half-broken is what makes every later HAL addition feel free
until someone tries to build it.

### Device Status must not poll

`SETTINGS_PROPOSAL.md` §4 says not to poll the shared I2C bus to animate settings,
and Device Status is where that gets violated, because a page of live numbers
invites a 1 Hz `lv_timer`. Battery rides the same bus as the touch controller, and
`DESIGN.md` §0 caps it at 30 s for exactly this reason.

Read once when the page opens; add a manual Refresh row. Heap and uptime are cheap
and could tick, but a page that refreshes half its values and not the others is
worse than one that refreshes none. Battery on this page reads the value the
service already cached — do not call `readBattery()` from the page at all.

## 7. Region & Time

### Timezone: a compiled catalog with DST rules (D4)

No list exists today, only the stored string. The catalog is compiled in, bounded,
and each entry carries its transition rules:

```cpp
struct TimezoneEntry { const char *label; const char *posix; };

constexpr TimezoneEntry kTimezones[] = {
    {"Hong Kong (UTC+08:00)",      "HKT-8"},
    {"Singapore (UTC+08:00)",      "SGT-8"},
    {"Tokyo (UTC+09:00)",          "JST-9"},
    {"Sydney (UTC+10:00)",         "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Dubai (UTC+04:00)",          "GST-4"},
    {"London (UTC+00:00)",         "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Berlin (UTC+01:00)",         "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"New York (UTC-05:00)",       "EST5EDT,M3.2.0,M11.1.0"},
    {"Chicago (UTC-06:00)",        "CST6CDT,M3.2.0,M11.1.0"},
    {"Los Angeles (UTC-08:00)",    "PST8PDT,M3.2.0,M11.1.0"},
    {"UTC",                        "UTC0"},
};
```

The shipped list is a product decision about markets. The *format* is not: an
entry for a DST-observing region without its rules is a clock that is right for
eight months. Note the POSIX sign convention is inverted from the label — `HKT-8`
is UTC+8 — which is why the label is stored separately and never derived.

Applying at runtime re-runs what `crystal_time_init()` does at boot:

```cpp
bool timezone_apply(const char *posix)
{
    if (posix == nullptr || posix[0] == '\0') return false;
    if (hal().storage != nullptr) {
        (void)hal().storage->set("timezone", posix, strlen(posix) + 1);
    }
    setenv("TZ", posix, 1);
    tzset();
    return true;
}
```

`tzset()` is required — `setenv` alone leaves `localtime_r()` on the old zone.
Exit criterion: the indicator bar hour moves without a reboot. It updates on
`update_clock()`'s next 1 s tick, so anything caching a `struct tm` is stale until
then. Nothing in this tree does, and it is worth not starting.

### Time format: the bar hardcodes 12-hour

`update_clock()` computes `hour_12` and passes `is_pm` unconditionally, and
`ESP_Brookesia_StatusBar::setClock(hour, minute, is_pm)` takes that shape. For
24-hour display, pass `now.tm_hour` with the format flag consulted:

```cpp
const bool format24 = time_format_24();
const int hour = format24 ? now.tm_hour : (now.tm_hour % 12 == 0 ? 12 : now.tm_hour % 12);
s_clock_update(s_status_context, hour, now.tm_min, !format24 && now.tm_hour >= 12);
```

Whether the Brookesia status bar renders an AM/PM suffix from `is_pm` needs
checking against the stylesheet; if it appends unconditionally, the 24-hour path
needs the bar's own format setting rather than a flag. Clock app reads the same
stored key — one setting, two consumers, no second copy of the rule.

### Automatic time and last sync

`network_signal_handler()` starts SNTP on every network-up with no user-visible
state. Add the gate at the top:

```cpp
if (id != CRYSTAL_NETWORK_CONNECTED || s_sntp_sync_started) return;
if (!time_auto_enabled()) return;    // new
```

`sntp_synced()` stores `time.last_sync` so the row can show it. When automatic
time is off, Set Date & Time enables and writes through `crystal_time_set()`,
which already updates both the system clock and the PCF85063.

Turning automatic time off does not stop an in-flight sync, and a manual time set
while a sync is pending will be overwritten. Cancel or ignore the pending sync
when the user commits a manual time — `esp_netif_sntp_deinit()` on the transition
is the simple version.

## 8. Location: replacing a cached global

Weather's location lives in `s_weather_latitude`, `s_weather_longitude`,
`s_weather_city` and the `s_weather_location_ready` flag, loaded once by
`resolve_weather_location()` on the service task. The NVS keys already exist and
`load_weather_location()` already range-checks. What is missing is a way to change
it at runtime.

```cpp
// crystal_core.hpp. Called from the LVGL task; stores, updates the service
// globals, and requests a refresh. Rejects out-of-range coordinates.
bool crystal_weather_set_location(double latitude, double longitude, const char *city);
```

Two things this must do that a naive setter misses:

- **Set `s_weather_location_ready`**, or the service task calls
  `resolve_weather_location()` on its next pass and the IP lookup overwrites the
  manual coordinates.
- **Reset the retry backoff** (`s_weather_next_try`), or a manual fix after a
  failed fetch waits out a backoff of up to 30 minutes with no visible reason.

Then `crystal_weather_request()` for an immediate refresh. Switching Location back
to Automatic clears `ready` and lets the resolver run again.

The globals are read on the service task and written from the LVGL task. They are
plain `double`/`char[]`, not atomics, and the fetch formats them into a URL. A
torn read here is a wrong URL, not a crash — but do the write through the same
`s_weather_request` handshake rather than assuming it is fine.

Validation is the range check `load_weather_location()` already applies: latitude
-90..90, longitude -180..180. Reject on commit; do not clamp silently.

## 9. Sound

Volume mirrors the quick panel: `crystal_hal_set_volume()` / `crystal_hal_get_volume()`,
0-100, persisted under `volume`. It is the same stored value, not a second setting.

Timer & Alarm Sounds is a flag checked where the chime is raised, not inside
`crystal_hal_timer_alarm()` — the HAL plays sound, policy lives above it. Test
Sound calls the same function directly and ignores the flag, since the user just
asked for it.

The codec initialises lazily on first use, so Test Sound is the first thing to
touch it on a fresh boot and carries that latency. Not a bug; worth not
misreading as one.

## 10. Copy and layout

Constraints from `DESIGN.md` §0 and §10, not suggestions: 44x44 px minimum touch
target, 16 px minimum body text, and three Montserrat sizes — 16 small, 20 medium,
28 large. 14 and 48 are also compiled in this build; the design's three are what
Settings uses. Row height of 56 px with 20 px labels and 16 px summaries fits the
480 px width without crowding.

Root rows carry a live summary, not a description: `WiFi  On - Home`, not
`WiFi  Configure wireless networks`. `SETTINGS_PROPOSAL.md` §2 has the layouts.

Disabled rows state their reason. "Manual IP fields" greyed with no explanation is
the same UI as broken; the reason line is "Automatic (DHCP) is on".

Destructive rows name their exact scope and confirm. None of the reset actions
land in Phase 11 — Restart is the only irreversible row here, and it still
confirms.

**Spelling.** This tree writes `WiFi`, unhyphenated, everywhere in code and UI
strings (`wifi_tile_text()`, "WiFi Networks"). `SETTINGS_PROPOSAL.md` uses `Wi-Fi`.
Pick `WiFi` for consistency with what is on screen today.

## 11. Documents to update

Phase 11 changes behaviour described elsewhere. These edits are part of the phase,
not follow-up:

- `DESIGN.md` §3 — page inventory rows for the five new subpages (D2).
- `DESIGN.md` §8 — the category table, and delete the "Divergence to settle in
  Phase 11" note once D1 is implemented.
- `DESIGN.md` §9 — Manage Apps moves to Phase 13.
- `DESIGN.md` §4 — the gesture table gains the bottom edge as a Crystal-owned
  gesture (D6), alongside the side edges and the top-right corner. Note that
  `enable_navigation_gesture` is cleared per app and why, so the next person to
  wonder where Brookesia's HOME went finds the answer in the design document
  rather than in a constructor.
- `DESIGN.md` §6 — the WiFi page's address changes: it is now a Network subpage,
  and the quick panel deep-links to it (D7).
- `CODE_GUIDE.md` §Phase 11 — replace with a pointer to this document.
- `IMPLEMENTATION_PLAN.md` §Phase 11 — the category list and exit criteria.
- `README.md` — tick Phase 11 when the exit criteria pass.
- `VALIDATION_CHECKLIST.md` — the regression rows from §13.
- `SETTINGS_PROPOSAL.md` — a note that D1-D7 are settled here.

## 12. Build order

Arranged so the exit criteria land first. Everything after step 4 is cuttable
without failing the phase, which is what makes "build it all, then trim" safe.
Steps 1-3 are not cuttable: they are the phase's structural work, and the
bottom-edge fix in particular is cheaper to do before there are five subpages to
re-test than after.

1. **Page and back stack** (§2). Root with five rows, one empty subpage, two-level
   back. Test Back from a subpage before adding any content to it.
2. **Bottom-edge fix** (§2.1). Do it here, not last. It changes how every gesture
   into and out of a system page resolves, and the WiFi page already gives you
   something to test it against before Settings has any content. Order inside the
   step: `CrystalApp` constructor first, confirm a bare bottom swipe still reaches
   the launcher, then add the arbiter's `Navigation` owner.
3. **Network** (§5). `IWifi` additions, connection details, DHCP/static form with
   validation, and folding the WiFi page into the stack (D7). Closes the static-IP
   exit criterion.
4. **Power** (§4). D1, stored values, Energy Saving's four effects. Closes one
   exit criterion and fixes a real behaviour bug.
5. **Region & Time** (§7). Timezone catalog and live apply. Closes the third exit
   criterion.
6. **System** (§6). `ISystemInfo`, About, Legal, Device Status, Restart.
7. **Sound** (§9), then Location (§8).
8. **Simulator mock** (§6). Do this before it is the last thing between the phase
   and a green build.
9. May slip, per the proposal: Device Name, hidden networks, temperature and wind
   units, low-battery auto-saving, Reduce Motion.

## 13. Exit criteria

From `IMPLEMENTATION_PLAN.md`, plus what the wider scope adds:

- Static IP survives a reboot, and a validated config reaches the interface while
  an invalid one never does.
- A static config with DNS set resolves hostnames; Weather still fetches.
- Timezone change moves the indicator bar hour without a reboot and survives one.
- Energy Saving measurably lowers current draw.
- Dim and off happen with Energy Saving **off** (D1), and Energy Saving shortens
  them rather than enabling them.
- Back from inside a subpage returns to the Settings root, not to a card.
- A bottom-edge swipe up with a system page open dismisses that page, not the card
  underneath it. Repeat until the stack is empty; the card is only reached last.
- A bottom-edge swipe up on a bare card still reaches the launcher (D6), and a tap
  in the bottom 24px of a card dismisses nothing.
- Long-pressing the quick panel's WiFi tile lands on WiFi Networks with Network
  and the root beneath it: Back walks out to Network, then the root, then the card
  (D7).
- There is exactly one WiFi SSID list in the build, reachable from Settings and
  from the quick panel, and closing it never leaves the Settings root without
  gesture ownership.
- Quick panel and Settings show the same brightness, volume, and Energy Saving
  values in both directions.
- A keyboard left open on the manual-IP form does not survive a subpage change.
- No `lv_*` call from the service task, and no I2C poll added to a settings page.
- The firmware builds; the simulator builds.

