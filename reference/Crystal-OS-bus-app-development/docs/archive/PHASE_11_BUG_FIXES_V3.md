# Phase 11 bug fixes V3 — closing the phase (code guide)

## Date: 2026-09-10

Five defects found in the 2026-09-10 device pass. Four of them hold Phase 11; the
fifth (bug 5) is cosmetic but visible on every settings form, so it belongs in the
same pass.

Contract for the phase is `PHASE_11_SETTINGS.md`; the device results are in
`VALIDATION_CHECKLIST.md` §Phase 11. This document is only about what is broken,
why, and the fix. Everything here was traced in the source rather than inferred
from the symptom — the LVGL findings in bug 5 in particular are against the
vendored 8.4.0 tree, not upstream master.

**Closed, 2026-09-11:** all five fixes are implemented and the
firmware passes both incremental and full-clean builds. Brookesia 0.4.2 is now a
project-local component, so its disabled-Recents guards survive managed dependency
regeneration. The complete V3 physical-panel rerun and the independent timer
alert-policy test pass. Phase 11 is closed.

| # | Defect | Blocks a validation row | Where |
|---|---|---|---|
| 1 | DHCP switch does not apply until Apply is pressed | no (behaviour) | `crystal_shell.cpp:2170` |
| 2 | Applying with DHCP on zeroes the stored static addresses | yes | `crystal_shell.cpp:2131` |
| 3 | Set Date & Time is unreachable | yes | `crystal_shell.cpp:2454` |
| 4 | Manual location rejects a city name silently | yes | `crystal_shell.cpp:2385` |
| 5 | Two blinking cursors across text fields | no (cosmetic) | `crystal_keyboard.cpp:209` |

Suggested order: 2, 1, 3, 4, 5. Bug 2 is data loss and the smallest change; bug 1
touches the same handler, so doing 2 first means the field-preservation rule is
already in place when the switch starts applying immediately.

---

## Bug 2: applying with DHCP on destroys the stored static addresses

**Severity: data loss.** Fix this one first.

### Symptom

Configure a static address, confirm it survives a reboot, then switch Automatic
(DHCP) on and press Apply. At the next boot the five address fields are empty and
the static configuration is gone. Nothing warned that Apply would discard it.

### Root cause

`ip_apply()` (`crystal_shell.cpp:2128`) declares `IWifi::IpConfig config = {};`
and, when `config.dhcp` is true, **skips the block that parses the five text
fields**. That is correct for applying DHCP — the addresses are not needed to start
the client — but the zeroed struct is then handed to `set_ip_config()`, which ends
with `persist_ip_config(config)` (`crystal_hal.cpp:534`), and that writes every
member unconditionally:

```cpp
return s_storage.set(kDhcpKey, &dhcp, sizeof(dhcp)) &&
       s_storage.set("net.ip", &config.ip, sizeof(config.ip)) &&
       // ... mask, gw, dns1, dns2 -- all zero on the DHCP path
```

So `net.ip` through `net.dns2` become 0. On the next boot `apply_stored_ip_config()`
reads `dhcp = 1` and returns early, which hides the damage; it only surfaces when
the user opens the page and finds the fields blank, or switches back to Manual and
has to retype everything.

Two independent things are wrong and both need fixing, because either one alone
leaves a hole:

1. `ip_apply()` sends addresses it never read.
2. `persist_ip_config()` treats "not supplied" and "zero" as the same value.

### Fix

**Part 1 — parse the fields regardless of mode; validate only when Manual.**
The addresses are the user's saved configuration, not an input to the DHCP call.
Read them so a round-trip through DHCP preserves them:

```cpp
void ip_apply(lv_event_t *event)
{
    lv_obj_t *dhcp_switch = static_cast<lv_obj_t *>(lv_event_get_user_data(event));
    IWifi::IpConfig config = {};
    config.dhcp = lv_obj_has_state(dhcp_switch, LV_STATE_CHECKED);

    // Parse whatever is in the fields either way. On the DHCP path these are not
    // applied to the interface, but they are still what gets persisted, so
    // dropping them here is what erased the user's static config.
    uint32_t *values[] = {&config.ip, &config.mask, &config.gateway,
                          &config.dns1, &config.dns2};
    bool complete = true;
    for (size_t i = 0; i < 5; ++i) {
        const char *text = lv_textarea_get_text(s_ip_fields[i]);
        const bool empty = text == nullptr || text[0] == '\0';
        if (empty) {
            if (i < 4) complete = false;   // dns2 is the only optional field
            continue;
        }
        if (inet_aton(text, reinterpret_cast<in_addr *>(values[i])) == 0) {
            // A malformed field is fatal on the Manual path and ignorable on the
            // DHCP path, but never worth persisting.
            *values[i] = 0;
            if (!config.dhcp) {
                lv_label_set_text(s_ip_apply_status,
                                  "Enter valid dotted-quad addresses");
                return;
            }
        }
    }

    if (!config.dhcp) {
        if (!complete) {
            lv_label_set_text(s_ip_apply_status, "Enter valid dotted-quad addresses");
            return;
        }
        // Existing mask/gateway/host checks, unchanged.
        const uint32_t mask = ntohl(config.mask);
        const uint32_t wildcard = ~mask;
        if (mask == 0 || (wildcard & (wildcard + 1)) != 0 ||
                (config.ip & config.mask) != (config.gateway & config.mask) ||
                (config.ip & ~config.mask) == 0 ||
                (config.ip & ~config.mask) == ~config.mask) {
            lv_label_set_text(s_ip_apply_status,
                              "Check subnet mask, gateway, and host address");
            return;
        }
    }
    // ... unchanged apply/status/summary tail
}
```

Note what did **not** change: an invalid or incomplete form on the Manual path
still never reaches `set_ip_config()`. That is the `DESIGN.md` §11 recovery
requirement and the fix must not weaken it.

**Part 2 — do not persist zeros over real values.** Part 1 is enough for the form,
but `persist_ip_config()` is reachable from `apply_stored_ip_config()` too, so make
the storage layer refuse to erase an address it was not given:

```cpp
static bool persist_ip_config(const IpConfig &config)
{
    const uint8_t dhcp = config.dhcp ? 1 : 0;
    if (!s_storage.set(kDhcpKey, &dhcp, sizeof(dhcp))) return false;

    // 0 means "not supplied", not "clear the stored value". Only a Manual apply
    // rewrites the addresses; a DHCP apply changes the mode and leaves the saved
    // configuration intact so switching back does not lose it.
    if (config.dhcp) return true;

    return s_storage.set("net.ip", &config.ip, sizeof(config.ip)) &&
           s_storage.set("net.mask", &config.mask, sizeof(config.mask)) &&
           s_storage.set("net.gw", &config.gateway, sizeof(config.gateway)) &&
           s_storage.set("net.dns1", &config.dns1, sizeof(config.dns1)) &&
           s_storage.set("net.dns2", &config.dns2, sizeof(config.dns2));
}
```

With both parts in, the mode is the only thing a DHCP apply writes, and the
addresses persist because nothing overwrote them — belt and braces, and the second
part is what protects any future caller.

### Verify

- Static config → reboot → still there (the row that already passed).
- Static config → DHCP on → Apply → reboot → open IP Settings: the five fields
  still hold the static values, mode reads Automatic.
- That same device → switch to Manual → Apply → the old static config applies with
  no retyping.
- Manual with a blank Primary DNS still refuses, and Weather still resolves after
  a valid static apply.

---

## Bug 1: the DHCP switch does not apply until Apply is pressed

### Symptom

Turning Automatic (DHCP) on changes the row summary and greys the fields, but the
interface keeps its static address until Apply is pressed. Nothing on screen says
so, and every other switch in Settings — WiFi, Auto Dimming, Energy Saving,
24-Hour Time, Timer Sounds — takes effect on the spot. The switch reads as broken.

### Root cause

The `LV_EVENT_VALUE_CHANGED` handler at `crystal_shell.cpp:2170` only touches the
UI:

```cpp
lv_obj_add_event_cb(dhcp_switch, [](lv_event_t *e) {
    const bool automatic = lv_obj_has_state(..., LV_STATE_CHECKED);
    set_ip_fields_enabled(!automatic);
    settings_row_set_summary(..., automatic ? "Router assigns..." : "Manual...");
}, LV_EVENT_VALUE_CHANGED, nullptr);
```

There is no `set_ip_config()` call and no NVS write, so `net.dhcp` keeps its old
value too. The design intent behind that was sound — §5 says "validate on commit,
not per keystroke", and a half-typed static form must never be applied — but it was
applied to the wrong control. Selecting *Automatic* has nothing to validate: it
needs no fields, and it is the recovery path a user reaches for after a bad static
config. Making it wait behind Apply is exactly backwards.

### Fix

Apply immediately in the one direction that cannot be invalid, and keep Apply as
the commit point for Manual:

```cpp
lv_obj_add_event_cb(dhcp_switch, [](lv_event_t *e) {
    const bool automatic = lv_obj_has_state(
        static_cast<lv_obj_t *>(lv_event_get_target(e)), LV_STATE_CHECKED);
    set_ip_fields_enabled(!automatic);
    settings_row_set_summary(lv_obj_get_parent(
        static_cast<lv_obj_t *>(lv_event_get_target(e))),
        automatic ? "Router assigns the address" : "Manual configuration");

    if (!automatic) {
        // Selecting Manual commits nothing: the form may be empty or half-typed,
        // and only Apply is allowed to reach the interface (§5).
        lv_label_set_text(s_ip_apply_status,
                          "Enter the addresses below, then press Apply");
        return;
    }

    // Automatic has nothing to validate and is the recovery path out of a bad
    // static config, so it takes effect at once. Bug 2's persist fix is what
    // keeps this from discarding the saved addresses.
    IWifi::IpConfig config = {};
    config.dhcp = true;
    if (hal().wifi != nullptr && hal().wifi->set_ip_config(config)) {
        lv_label_set_text(s_ip_apply_status, "Automatic addressing enabled");
        settings_row_set_summary(s_network_details_row, "Connected - DHCP");
    } else {
        lv_label_set_text(s_ip_apply_status, "Could not enable automatic addressing");
    }
}, LV_EVENT_VALUE_CHANGED, nullptr);
```

**Order matters: land bug 2 first.** Applied on its own, this makes the switch call
`set_ip_config()` with a zeroed struct on every toggle, which triggers bug 2
immediately rather than only when Apply is pressed.

Also worth knowing: `set_ip_config()` reconnects when a station is configured
(`esp_wifi_disconnect()` then `esp_wifi_connect()`), so an immediate apply drops
the link for a moment. That is expected and is why §5 says it is not safe to call
mid-scan. It does not need a confirm dialog — the user asked for DHCP.

### Verify

- Static config applied, then DHCP on: the address changes to a lease with no
  Apply press, and Connection Details reflects it.
- Reboot: still DHCP, and the static values are intact per bug 2.
- Manual selected: nothing is applied, the hint appears, and Apply still validates.
- Toggle DHCP on and off several times with no reboot: no crash, no orphaned
  keyboard, and the WiFi icon recovers each time.

---

## Bug 3: Set Date & Time is unreachable

### Symptom

"No manual time page." Turning Set Time Automatically off ungreys the Set Date &
Time row, but tapping it does nothing. The page exists and is reachable only if
automatic time was *already* off when Region & Time was built — which, since
`time.auto` defaults to 1, is never true on a fresh device.

### Root cause

`settings_push_region_time()` binds the handler inside an `else`
(`crystal_shell.cpp:2454`):

```cpp
lv_obj_t *manual = settings_row(content, "Set Date & Time", ...);
s_region_manual_time_row = manual;
if (crystal_time_auto_enabled()) lv_obj_add_state(manual, LV_STATE_DISABLED);
else lv_obj_add_event_cb(manual, [](lv_event_t *) { settings_push_manual_time(); },
                         LV_EVENT_CLICKED, nullptr);
```

The row's enabled state is dynamic — the automatic-time switch handler clears
`LV_STATE_DISABLED` at `crystal_shell.cpp:2440` — but the *callback* is bound once
at build time. Flip the switch off in place and the row looks enabled and has no
handler. The only way to reach the page is to leave Region & Time and come back so
it is rebuilt with `crystal_time_auto_enabled()` already false.

This is a general trap, not a one-off: **bind handlers unconditionally and gate on
state inside them.** `LV_STATE_DISABLED` on an LVGL object suppresses the click
itself, so the guard is redundant for input — but it is worth keeping as a
belt-and-braces check because `settings_row()` targets are plain objects whose
disabled styling is easy to lose in a later refactor.

### Fix

```cpp
lv_obj_t *manual = settings_row(content, "Set Date & Time",
    crystal_time_auto_enabled() ? "Turn automatic time off first" : "Manual");
s_region_manual_time_row = manual;
if (crystal_time_auto_enabled()) lv_obj_add_state(manual, LV_STATE_DISABLED);
// Bind unconditionally. The row's disabled state changes while the page is open,
// so a handler attached only when automatic time was already off leaves a dead
// row after the user flips the switch in place.
lv_obj_add_event_cb(manual, [](lv_event_t *) {
    if (crystal_time_auto_enabled()) return;   // re-checked, not assumed
    settings_push_manual_time();
}, LV_EVENT_CLICKED, nullptr);
```

While in this handler, check the same pattern on the sibling rows. `Location` at
`crystal_shell.cpp:2458` is bound unconditionally and is fine; the timezone and
format rows are too. This is the only row with the defect.

### Also fix: the Set Date & Time page gives no feedback

Once the page is reachable, its Apply handler (`crystal_shell.cpp:2414`) `return`s
silently on every parse or range failure, so a typo looks identical to a successful
set. It also depends on child indices 0 and 1 to find its two fields, which breaks
the moment a row is inserted above them. Add a status label like the IP form's and
capture the fields in the closure rather than looking them up by index:

```cpp
// Same shape as the IP form: one status label, messages on failure, and no
// silent returns. lv_obj_get_child(content, 0/1) is avoided deliberately --
// inserting any row above the fields would silently repoint it.
lv_obj_t *status = lv_label_create(content);
lv_label_set_text(status, "");
lv_obj_set_style_text_color(status, lv_color_hex(0xf59e0b), 0);
```

Report the two failure classes separately — "Use YYYY-MM-DD and HH:MM" for a parse
failure and "Check the date and time values" for an out-of-range one — because they
need different corrections. Keep the existing `crystal_time_set()` call and the
`system_page_pop()` on success; that part is right, and it already writes both the
system clock and the PCF85063.

### Verify

- Fresh boot → Region & Time → switch automatic time off → tap Set Date & Time:
  the page opens.
- Leave and re-enter Region & Time with automatic off: still opens.
- Automatic on: the row is greyed, reads "Turn automatic time off first", and does
  nothing when tapped.
- Set a time, confirm the indicator bar updates, reboot without network, and
  confirm the RTC held it.
- A malformed date and an out-of-range date each produce their own message.
- Turning automatic time back on with a sync pending: per §7, commit cancels or
  ignores the in-flight sync. Confirm a manual set is not overwritten seconds later.

---

## Bug 4: manual location rejects a city name with no explanation

### Symptom

On Location, entering `Taipei` in the city field and pressing Apply Manual Location
does nothing at all — no message, no page change. The user reasonably reads the
city field as the way to set a location.

### Root cause

Two separate problems.

**There is no geocoding.** `crystal_weather_set_location(latitude, longitude, city)`
takes coordinates; the city string is a display label only, stored under
`weather.city` and shown in the UI. Nothing resolves a name to coordinates, and
§8's validation is the range check `load_weather_location()` already applies. This
is by design and is not worth changing in Phase 11 — a geocoding lookup means
another HTTP dependency and a failure mode on a page whose job is to *fix* a bad
location. But the UI does not say so.

**Every failure path returns silently.** The Apply handler
(`crystal_shell.cpp:2385`) has three bare `return`s — empty lat/lon, unparseable
lat, unparseable lon — and `crystal_weather_set_location()` returning false is
discarded. So an empty coordinate field, a typo, and an out-of-range value all
produce identical nothing.

It also reads its fields with `lv_obj_get_child(content_obj, 1/2/3)`, which is the
same index fragility as bug 3 and is already load-bearing here: the switch handler
at `crystal_shell.cpp:2374` iterates children 1-3 to grey them.

### Fix

Make the requirement legible, then report every failure:

```cpp
// The city is a label, not a lookup key. Say so in the placeholder so the field
// does not read as a way to set the location.
lv_textarea_set_placeholder_text(city, "City label (display only)");
lv_textarea_set_placeholder_text(lat, "Latitude -90 to 90 (required)");
lv_textarea_set_placeholder_text(lon, "Longitude -180 to 180 (required)");
```

Add a status label to the page and replace the silent returns:

```cpp
const char *lat_text = lv_textarea_get_text(lat_field);
const char *lon_text = lv_textarea_get_text(lon_field);
if (lat_text == nullptr || lat_text[0] == '\0' ||
    lon_text == nullptr || lon_text[0] == '\0') {
    lv_label_set_text(s_location_status,
                      "Enter latitude and longitude. A city name alone cannot "
                      "set the location.");
    return;
}
char *end = nullptr;
const double latitude = strtod(lat_text, &end);
if (end == nullptr || *end != '\0') {
    lv_label_set_text(s_location_status, "Latitude must be a number, e.g. 25.03");
    return;
}
const double longitude = strtod(lon_text, &end);
if (end == nullptr || *end != '\0') {
    lv_label_set_text(s_location_status, "Longitude must be a number, e.g. 121.57");
    return;
}
if (!crystal_weather_set_location(latitude, longitude,
                                  lv_textarea_get_text(city_field))) {
    // Rejected on range, per §8: reject rather than clamp, and say which bound.
    lv_label_set_text(s_location_status,
                      "Latitude must be -90 to 90 and longitude -180 to 180");
    return;
}
```

Add `s_location_status` to the `s_*` pointers cleared in the page teardown
(`crystal_shell.cpp:1937` is where the sibling row pointers are cleared). Missing
that is the stale-pointer trap §2 warns about.

Consider also giving the page a worked example row — `Taipei is 25.03, 121.57` — so
the coordinate requirement has an answer on screen and not just a constraint. That
is a judgement call, not a defect; it is cheap and it is the page a user reaches
when Weather is showing the wrong city.

If the city label is left empty on a successful apply, keep whatever
`weather.city` held rather than storing an empty string, or Weather renders a blank
location. Worth checking `crystal_weather_set_location()`'s handling of `nullptr`
and `""` while here.

### Verify

- City name only → Apply: a message states coordinates are required.
- `25.03` / `121.57` → Apply: the page pops, Weather refreshes immediately (§8's
  backoff reset), and the Region & Time summary reads Manual.
- `abc` in latitude → Apply: a parse message, no commit.
- `120` in latitude → Apply: a range message, no commit.
- Switching Location back to Automatic lets the resolver run again and survives a
  reboot.

---

## Bug 5: two cursors blinking in two different fields

### Symptom

Move between text fields on a settings form — IP Settings has five, Location has
three — and a cursor is sometimes left blinking in a field that is no longer
focused, so two fields blink at once. It is cosmetic but it makes the form look
broken, and it says the wrong thing about where typing will land.

### Root cause

This is an interaction between Crystal's keyboard rebinding and how LVGL 8.4 tracks
click focus. Three facts, each verified in `managed_components/lvgl__lvgl`:

**1. The cursor is drawn from `LV_STATE_FOCUSED`, via the theme.**
`lv_theme_default.c:1001` attaches the cursor style as
`LV_PART_CURSOR | LV_STATE_FOCUSED`, so a textarea that keeps `LV_STATE_FOCUSED`
keeps drawing a cursor. `lv_textarea.c` starts an infinite blink animation on
`LV_EVENT_FOCUSED` (line 873) and **never handles `LV_EVENT_DEFOCUSED` at all** —
`grep -c LV_EVENT_DEFOCUSED lv_textarea.c` returns 0. Losing the state is what
stops the cursor; `lv_obj.c:838` is what clears it, on `LV_EVENT_DEFOCUSED`.

**2. Defocus of the previous field depends on `last_pressed`.** These fields are in
no group, so `indev_click_focus()` (`lv_indev.c:1049`) takes the "not in a group"
branch: send `LV_EVENT_DEFOCUSED` to `proc->types.pointer.last_pressed`, then
`LV_EVENT_FOCUSED` to the new object, then — **at the very end, line 1111** — set
`last_pressed` to the new object.

**3. Crystal resets the indev in the middle of that sequence.** The fields bind
`ip_field_focus` to `LV_EVENT_FOCUSED` (`crystal_shell.cpp:2113`), which calls
`crystal_keyboard_show()`, whose first act for a different field is
`crystal_keyboard_hide()` (`crystal_keyboard.cpp:209`). That calls
`lv_indev_reset(nullptr, keyboard)` (line 318), which sets `reset_query = 1` on
every indev. Back in `indev_click_focus()`, the `indev_reset_check()` immediately
after the FOCUSED send returns true and the function **returns before line 1111**.

So `last_pressed` is never updated to the field just focused. Walking A → B → C:

| Tap | DEFOCUSED sent to | FOCUSED sent to | `last_pressed` after |
|---|---|---|---|
| A | (none) | A | still null — reset fired |
| B | null → nobody | B | still null |
| C | null → nobody | C | still null |

A, B and C all hold `LV_STATE_FOCUSED` and all three blink. In practice one of them
usually loses the state for other reasons, which is why the report is "sometimes,
two cursors" rather than "always, all of them".

Note this is *not* the upstream `lv_keyboard_set_textarea()` bug, though that exists
too and is worth knowing about: `lv_keyboard.c` clears and sets `LV_STATE_FOCUSED`
on the **keyboard** rather than on the textarea, and passes `LV_STATE_FOCUSED` to
`lv_obj_add_flag()` where a flag is expected. Its cursor-management intent does not
work. Do not rely on it, and do not patch it — Crystal creates a fresh keyboard per
field anyway.

### Fix

Crystal changes the focused field, so Crystal should defocus the one it is leaving.
Do it in `crystal_keyboard_hide()`, where the old field is still known, rather than
in `show()` — `hide()` is the single teardown path that every close route already
funnels through (Done, Cancel, background tap, field deletion, rebinding).

```cpp
void crystal_keyboard_hide()
{
    if (s_hiding) return;
    s_hiding = true;
    crystal_shell_set_keyboard_open(false);

    // The field keeps LV_STATE_FOCUSED until something sends it DEFOCUSED, and
    // lv_textarea has no DEFOCUSED handler -- the theme draws the cursor from the
    // state, so a field left focused keeps blinking. LVGL cannot do this for us
    // here: lv_indev_reset() below aborts indev_click_focus() before it updates
    // last_pressed, so the next tap defocuses the wrong object. See V3 bug 5.
    if (s_field != nullptr) {
        lv_event_send(s_field, LV_EVENT_DEFOCUSED, nullptr);
    }
    // ... existing viewport restore and teardown
```

`lv_event_send()` rather than `lv_obj_clear_state()` deliberately: the object's own
`LV_EVENT_DEFOCUSED` handler clears `LV_STATE_FOCUSED | LV_STATE_EDITED |
LV_STATE_FOCUS_KEY` together (`lv_obj.c:839`), and any field-level listener gets
told as well.

Place it before the `lv_indev_reset()` on the keyboard and before `s_field` is
cleared. It must run inside the `s_hiding` guard, because sending an event to the
field can re-enter `crystal_keyboard_hide()` through
`watched_object_deleted`/`viewport_click_event` — the guard is what makes that safe,
and it is already there.

### Why not the alternatives

- **Put the fields in a group.** LVGL would then route focus through
  `lv_group_focus_obj()` and defocus the previous member itself. It is the
  idiomatic fix and it is a bigger change: something has to own the group's
  lifetime per page, `lv_group_focus_obj()` is also subject to the same
  `indev_reset_check()` abort, and the shell currently uses the default group only
  at `crystal_shell.cpp:1772`. Not worth it for a cursor.
- **Set `LV_PART_CURSOR` opacity to transparent on the old field.** Works, but it
  fights the theme's state-based styling and leaves `LV_STATE_FOCUSED` set, so the
  field still claims to be focused to anything else that asks.
- **Patch `lv_textarea` to handle `LV_EVENT_DEFOCUSED`.** Correct upstream, but it
  is a vendored-dependency patch with the same durability problem as the Brookesia
  Recents fix (see below). The one-line send in Crystal's own teardown gets the same
  result with nothing to reapply.

### Verify

- IP Settings: tap each of the five fields in turn. Exactly one cursor at a time.
- Tap field 1, then field 5, then field 1 again: no cursor left behind anywhere.
- Location: same across city/lat/lon, including after toggling Automatic.
- Close the keyboard with Done, then with a background tap, then by popping the
  subpage: no field is left blinking in any of the three routes.
- Type after switching fields: the text lands in the field that shows the cursor.
- Regression, because this touches the shared teardown: the Phase 10 keyboard rows
  in `VALIDATION_CHECKLIST.md` — viewport restore, scroll position, WiFi credential
  dialog, Calculator — all still behave.

---

## Closed: the Brookesia Recents patch is durable

This item is no longer open. Crystal owns the pinned Brookesia 0.4.2 component at
`components/esp-brookesia`, including the disabled-Recents null-safety guards.
ESP-IDF gives this project-local component precedence over the registry-managed
copy, so dependency regeneration cannot replace the fix.

The old force-tracked manager source under `managed_components/` was removed. On
2026-09-11, `idf.py fullclean` removed and resolved managed dependencies, configure
selected `components/esp-brookesia`, and the subsequent firmware build completed.
The durable override is also recorded in `PHASE_11_SETTINGS.md` §11 and
`HOME_PILL_FEEDBACK_AND_RECENTS_HOTFIX.md`.

The durability problem is therefore closed. Repeated physical side-switching is
still an acceptance test for the Recents behavior, not outstanding implementation
work.

## Bug Fixes V3 checklist

### Build and durability

- [x] The standalone HAL mock passes its C++ syntax check.
- [x] An incremental ESP-IDF firmware build completes.
- [x] `idf.py fullclean` followed by a clean build completes.
- [x] Clean configure selects `components/esp-brookesia` instead of the managed
  Brookesia copy.
- [x] The application image fits the smallest 5 MB OTA slot.
- [x] The application image flashes with hash verification and boots without a
  panic, watchdog reset, or reboot loop.

### Bugs 1 and 2: DHCP and saved static configuration

- [x] Apply a valid static IP, subnet mask, gateway, and primary DNS; reboot and
  confirm all values survive and Weather still resolves hostnames.
- [x] Turn Automatic on and confirm DHCP applies immediately without pressing
  Apply; Connection Details changes to the leased address after reconnection.
- [x] Reboot in Automatic mode and confirm Automatic remains selected while the
  saved static fields retain their previous values.
- [x] Switch back to Manual, press Apply, and confirm the retained static
  configuration applies without retyping it.
- [x] Confirm blank, malformed, or internally inconsistent required Manual fields
  show an error and never reach the network interface.
- [x] Toggle Automatic and Manual repeatedly and confirm reconnection completes
  without a crash or an orphaned keyboard.

### Bug 3: manual date and time

- [x] On a fresh Region & Time page, turn Set Time Automatically off and confirm
  Set Date & Time opens immediately without leaving and re-entering the page.
- [x] Re-enter Region & Time with automatic time already off and confirm the row
  still opens.
- [x] Turn automatic time on and confirm the row is disabled and cannot open.
- [x] Enter malformed text and confirm the format message appears without changing
  the clock.
- [x] Enter an impossible or out-of-range date/time and confirm the value message
  appears without changing the clock.
- [x] Apply a valid date/time, reboot without network access, and confirm the RTC
  restores it.
- [x] Re-enable automatic time and confirm an in-flight synchronization cannot
  overwrite a later manual commit unexpectedly.

### Bug 4: manual location

- [x] Enter only a city label and confirm Apply explains that latitude and
  longitude are required.
- [x] Enter non-numeric latitude or longitude and confirm the corresponding parse
  message appears without committing.
- [x] Enter coordinates outside `-90..90` latitude or `-180..180` longitude and
  confirm the range message appears without committing.
- [x] Apply a valid city label and coordinates and confirm the page closes, the
  Region & Time summary reads Manual, and Weather refreshes immediately.
- [x] Reboot and confirm manual location mode and values survive.
- [x] Re-enter the Location page and confirm its city, latitude, and longitude
  fields are populated from the saved manual values. **Follow-up implemented:**
  the page now hydrates these fields from the same NVS values used by Weather,
  following IP Settings' stored-field initialization pattern.
- [x] Return Location to Automatic and confirm automatic resolution resumes and
  survives reboot.

### Bug 5: keyboard focus and cursor ownership

- [x] In IP Settings, tap all five fields in sequence and confirm exactly one
  cursor blinks at a time.
- [x] In Location, move among city, latitude, and longitude and confirm no cursor
  remains in the previous field, including after toggling Automatic.
  **Follow-up implemented:** moving between fields in the same viewport now
  rebinds the existing keyboard instead of restoring and resizing the page during
  the focus handoff.
- [x] Close the keyboard using Done, a background tap, and subpage Back; confirm no
  field is left focused after each route.
- [x] Switch fields and type; confirm text always enters the field showing the
  cursor.
- [x] Re-run the Phase 10 keyboard checks for viewport restoration, scroll
  position, the WiFi credential dialog, and Calculator.

### Disabled Recents regression

- [x] Repeatedly side-switch among apps with `max_running_num = 1`; confirm the old
  app closes, the new app starts, and no null dereference or reboot occurs.
- [x] Perform long bottom-edge drag-and-hold gestures and confirm Recents never
  opens or changes app lifecycle unexpectedly.

### Phase 11 closure

- [x] Complete the remaining hardware row in `VALIDATION_CHECKLIST.md`
  §Phase 11: run one Clock timer expiry with alerts enabled and one disabled.
- [x] Confirm every V3 bug-fix and regression item above passes on the physical
  panel.
- [x] Mark Phase 11 complete in `README.md`; retain this document as the defect and
  validation record rather than an open work-list.
