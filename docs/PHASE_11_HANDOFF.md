# Phase 11 — session handoff

Written at the end of the planning session. The deliverable is
`docs/PHASE_11_SETTINGS.md`; this file records what was decided, what was verified
against the source, and what a fresh session should know before touching code.

No code was changed this session. Only two documents were written.

## What was produced

- **`docs/PHASE_11_SETTINGS.md`** — created. The Phase 11 code guide. Sections 0-13.
  This is the document to work from.
- **`docs/SETTINGS_PROPOSAL.md`** — edited in six places: a pointer to the
  decisions, the DST-rules requirement on the Timezone row, the HAL-units note on
  Dim Brightness, read-once semantics on Device Status, placement of Reset Network
  Settings, and the simulator-stub note in §5.

Working tree also shows `reference/ESP32-S3-Touch-LCD-4B` modified. That predates
this session and was not touched.

## Scope decision

Full proposal scope, build-then-trim. The critique offered mid-session was that
scope had roughly tripled and that Region & Time should drop out of must-ship;
that was overridden deliberately — implement everything, cut what turns out
unnecessary afterwards. §12's build order exists to make later cuts safe: the exit
criteria land in steps 1-4, and everything after step 4 is cuttable without
failing the phase.

## The seven decisions

**D1** Timeouts always apply; Energy Saving only shortens them. `check_power_state()`
currently evaluates Dim and Off only inside `if (energy_saving_enabled())`, so with
the toggle off the panel never dims. `DESIGN.md` §8 wins. This is a user-visible
change on every device that updates, which makes the shipped defaults a product
decision (§4).

**D2** Root categories are Network, Display & Power, Sound, Region & Time, System.
Supersedes `DESIGN.md` §8. Manage Apps moves to Phase 13.

**D3** Settings is a shell-owned page, not a `CrystalApp`. `DESIGN.md` §5.5 already
required it.

**D4** Timezone entries carry full POSIX DST rules. A picker storing `EST5` is an
hour wrong in March.

**D5** Dim Brightness is raw HAL units on the 0-95 scale, range 5-50 — not a
percentage of it. `dim_brightness()` compares against `hal().brightness->get()`
directly.

**D6** Crystal owns the bottom edge; Brookesia's per-app gesture navigation is
cleared. The launcher stays and remains the destination for a bottom swipe on a
bare card.

**D7** One WiFi Networks page, living under Network. The quick panel deep-links
into it rather than opening a separate page.

## The architecture question, and why Settings stayed an overlay

The long-standing annoyance: swipe up from the bottom with the WiFi page open and
the page survives while the card behind it is dismissed. The question raised was
whether settings-as-overlay is the wrong pattern and Settings should instead be an
app with special flags, iPhone-style.

Conclusion: the overlay is not the problem, and promoting Settings to a
`CrystalApp` would not have fixed it. HOME would still pause Settings and go to the
launcher, because the defect is one unwired input path, not an overlay-design
failure. Promotion would also have cost two card rebuilds per visit against the
80ms budget, plus a launcher-icon-or-upstream-patch dilemma for hiding it from the
ring.

Framing that settled it: settings-as-overlay is the appliance pattern (car head
units, TVs, thermostats); settings-as-app is the phone pattern. `DESIGN.md` §0
resolves ties toward appliance, and §5.5 had already decided.

**The launcher stays.** Reasoning was explicitly the user's: a few large apps would
favour removing it for a slightly bigger window with the nav bar gone, but many
small apps make its absence harmful, and which case applies is the user's choice —
so it sits there. This narrowed the fix: HOME has a legitimate destination for a
bare card, so the bottom swipe must fall through to the launcher rather than being
made inert.

## The bug, root-caused

Two independent listeners on the same gesture object, and only one knows the shell
has a front layer.

Brookesia's `onGestureNavigationReleaseEventCallback`
(`esp_brookesia_phone_manager.cpp:692`) fires `sendNavigateEvent(HOME)` on
`GESTURE_DIR_UP`. `processNavigationEvent()` (line 567) handles HOME with
`processAppPause()` + `processHomeScreenChange(MAIN)` + `resetActiveApp()` and
**consults no hook anywhere**.

That is the asymmetry. BACK routes through `active_app->back()` →
`CrystalApp::back()` (`crystal_app.cpp:175`) → `s_shell_back_hook()` (line 184) →
`shell_consume_back()` (`crystal_shell.cpp:1532`). HOME has no equivalent. So Back
correctly closes the WiFi page and a bottom swipe correctly ignores it.

Three things ruled out as fixes:

- **The arbiter alone.** It does lock `CrystalGestureOwner::App` when a front layer
  is open, but ownership is advisory between *Crystal's own* handlers. Brookesia's
  callback never reads `s_gesture_owner`.
- **Callback ordering.** The shell registers in `crystal_shell_init()`, which runs
  after `phone->begin()` in `main.cpp`, so Brookesia's handler always fires first.
- **Reaching into the manager.** `processNavigationEvent()` and every
  `_flags.enable_gesture_navigation_*` are private, with no public setter.

## The fix, and the constructor problem inside it

The fix mirrors what already works for card switching. Left/right edges work
because the 480x480 stylesheet ships `enable_gesture_navigation_back = 0`, so
Brookesia never competes for them. Do the same for the bottom.

`processGestureScreenChange()` (`esp_brookesia_phone_manager.cpp:325`) computes
`enable_gesture_navigation` from `app_data->flags.enable_navigation_gesture` at
every screen change, and `enable_gesture_navigation_home` is gated on it. Clear
that flag and Brookesia raises neither HOME nor recents from a gesture.

**The obstacle.** The flag lives in `_init_data`, which is private in
`ESP_Brookesia_PhoneApp` (`esp_brookesia_phone_app.hpp:116`) with only a const
`getActiveData()`. So it cannot be cleared after construction. `CrystalApp`'s
current `ESP_Brookesia_PhoneApp(name, launcher_icon, true)`
(`crystal_app.cpp:90`) hardcodes the default macro.

**The route.** The 2-arg `ESP_Brookesia_PhoneApp(core_data, phone_data)` constructor
(`esp_brookesia_phone_app.hpp:33`), handed data built by a helper. The helper exists
because the macro is a brace initialiser and cannot be overridden inline. Full code
is in §2.1 of the guide. No upstream patch; the change is confined to `CrystalApp`.

Four things verified against source rather than assumed:

- **The data is copied, not referenced.** `_core_init_data(data)`
  (`esp_brookesia_core_app.cpp:29`) and `_init_data(phone_data)`
  (`esp_brookesia_phone_app.cpp:18`) are both by-value into plain struct members.
  So returning a temporary from the helper and binding it to a `const &` parameter
  is safe. Nothing about this approach would work if either base held a pointer.
- **The macro arguments match the 3-arg path.**
  `ESP_BROOKESIA_CORE_APP_DATA_DEFAULT(name, launcher_icon, use_default_screen)`
  (`esp_brookesia_core_app.cpp:48`) and
  `ESP_BROOKESIA_PHONE_APP_DATA_DEFAULT(launcher_icon, true, false)`
  (`esp_brookesia_phone_app.cpp:33`). Pass exactly those and the swap is
  behaviour-neutral apart from the cleared bit.
- **The flag reaches the manager.** `beginExtra()` does `_active_data = _init_data`
  (`esp_brookesia_phone_app.cpp:74`) at install; the manager reads
  `getActiveData()`.
- **Upstream already does this.** `beginExtra()` clears
  `enable_navigation_gesture` itself when no gesture object exists
  (`esp_brookesia_phone_app.cpp:82-84`). Clearing it deliberately is the same
  operation, not a new kind of change.

**Do not use `const_cast` on `getActiveData()` instead.** It would appear to work —
install happens once at boot and nothing rewrites `_active_data` afterwards — which
is exactly the trap: it depends on install-once timing that nothing enforces, and
`delExtra()` zeroes the struct, so any uninstall/reinstall silently loses the flag.

One correction made during the session and worth not re-deriving: an earlier claim
that a `const_cast` write would be reverted "at the next screen change" was wrong.
`_active_data` is written in `beginExtra()` at install, not per screen change. The
reason to avoid the cast is the timing dependency above, not reversion.

**Then the arbiter handles the gesture.** `kBottomBand = 24` matching `kEdgeBand`, a
new `CrystalGestureOwner::Navigation`, claimed in `on_gesture_pressing()` before the
`horizontal_edge` test. Two cases must not claim it: the keyboard, and an open quick
panel where an upward drag already dismisses the panel.

Release order is `shell_consume_back()` first, then
`sendNavigateEvent(ESP_BROOKESIA_CORE_NAVIGATE_TYPE_HOME)` only if nothing of the
shell's was on top. `sendNavigateEvent()` is public on `ESP_Brookesia_Core`
(`esp_brookesia_core.hpp:49`), so the launcher path is Brookesia's own code reached
deliberately instead of by accident. Reusing `shell_consume_back()` rather than
writing a second dismissal chain is the point: one swipe peels one layer exactly as
one Back does.

**Known cost, accepted not worked around.** `enable_gesture_show_mask_bottom_edge`
and `enable_gesture_show_bottom_indicator_bar` are computed from
`enable_gesture_navigation`, so clearing it also removes Brookesia's bottom
indicator pill, and Crystal apps run with the nav bar hidden so nothing replaces it.
If it is missed, the shell can draw its own pill. **Do not** set
`navigation_bar_visual_mode = SHOW_FLEX` to get it back — that disables gesture HOME
as a side effect and puts a real nav bar on screen.

## D7: one WiFi list

The page `wifi_page_open()` builds (`crystal_shell.cpp:1553`) *becomes* Settings ›
Network › WiFi Networks. It joins the system-page stack instead of creating and
deleting itself independently; its Back button pops one level; its cached-pointer
clearing in `wifi_page_close()` survives verbatim as the model other subpages copy.

This also dissolves a real bug that existed independently: `wifi_page_open()` sets
`crystal_shell_set_settings_open(true)` and `wifi_page_close()` sets it false, so
opened from inside Settings the close would clear the flag while the Settings root
was still up, handing gestures back to the card underneath. The flag becomes
`s_system_page_depth > 0` — derived from the stack, so it cannot disagree with what
is on screen.

The quick panel's WiFi long-press deep-links: root → Network → WiFi Networks, so
backing out walks through Network to the root and only then to the card. Opening it
bare would leave the user somewhere with no way back into Settings — the isolated-
overlay behaviour this phase removes. Three pages on one tap may exceed the 80ms
budget; measure it, and if it is slow build the leaf and mark intermediates lazily.
The stack entries must exist either way, because `system_page_pop()` needs somewhere
to return to. Do not solve it by making WiFi a root-level page again.

## Entry points

Both in `components/crystal_shell/src/crystal_shell.cpp`. This was missing from the
guide until the end of the session and is now §2's opening subsection.

- **Gear tile**, line 311 — closes the panel then calls `quick_show_message()`,
  which shows `"Settings coming in Phase 11"` (line 210). Replace the toast with
  `settings_open()`. Keep the panel-close animation. Check whether
  `quick_show_message()` has other callers before deleting it.
- **WiFi tile long-press**, line 284 — currently
  `close_quick_settings(wifi_page_open)`, becomes
  `close_quick_settings(settings_open_at_wifi)`. `close_quick_settings()` takes the
  follow-up as a callback so it runs after the close animation; use it rather than
  opening the page first.

No third entry point, and Phase 11 does not add one.

## Before writing code

**The simulator mock is already broken.** `sim/crystal_hal_mock.cpp` does not
compile against the current `crystal_hal.hpp` — it predates `has_ip()` and
`IPower`. "The simulator builds" is failing *before* Phase 11 adds
`IWifi::set_ip_config()` and `ISystemInfo` to it. §12 lists the mock as step 8;
don't discover this at the end.

**Function names in the guide are proposed, not existing.** `settings_open()`,
`system_page_push()`, `settings_open_at_wifi()` — none exist yet. Grepping for them
and finding nothing is expected.

**Steps 1-3 of §12 are not cuttable.** The bottom-edge fix is step 2 deliberately:
the `CrystalApp` constructor swap touches every app in the build, so if it breaks
something you want to find out while the only thing to re-test is the WiFi page, not
five subpages. Order inside the step: constructor first, confirm a bare bottom swipe
still reaches the launcher, then add the `Navigation` owner. If a bare swipe stops
reaching the launcher, the arbiter is claiming and not forwarding, and every other
test in §2.1 will mislead.

**Spelling.** This tree writes `WiFi` unhyphenated everywhere in code and UI
strings. `SETTINGS_PROPOSAL.md` uses `Wi-Fi`. `WiFi` wins, for consistency with
what is on screen today.

## Still to do, deferred by design

§11 of the guide lists document edits that are part of the phase, none of them done
yet: `DESIGN.md` §3 (subpage inventory), §4 (gesture table gains the bottom edge and
why `enable_navigation_gesture` is cleared), §6 (the WiFi page's new address), §8
(category table, and delete the "Divergence to settle in Phase 11" note), §9 (Manage
Apps to Phase 13); `CODE_GUIDE.md` Phase 11 replaced with a pointer here;
`IMPLEMENTATION_PLAN.md` Phase 11; `README.md` phase tick;
`VALIDATION_CHECKLIST.md` regression rows from §13.
