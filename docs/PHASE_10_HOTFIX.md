# Phase 10 hot fix — keyboard overlay

Phase 10 shipped in `10a2aee` and builds clean, but four behaviours diverge from
`DESIGN.md` §7 and one validation record is missing. This guide is the work plan
for closing them. Phase 10 stays open until every item here is done and the
device checks in §6 are recorded.

Files in scope:

- `components/crystal_shell/src/crystal_keyboard.cpp`
- `components/crystal_shell/src/crystal_shell.cpp`
- `components/crystal_app/include/crystal_app.hpp`
- `components/crystal_app/src/crystal_app.cpp`
- `components/dev_tester/src/dev_tester_app.cpp`
- `docs/VALIDATION_CHECKLIST.md`

---

## 1. Done must not delete the keyboard inside its own event

**Symptom.** Typed text stays in the field, but nothing that waits on
"input committed" runs. Dev Tester's output label never updates.

**Cause.** `crystal_keyboard.cpp:249` binds `keyboard_close_event` to
`LV_EVENT_READY`, and that handler calls `crystal_keyboard_hide()`, which calls
`lv_obj_del(keyboard)`. LVGL's own OK branch in `lv_keyboard_def_event_cb`
(`managed_components/lvgl__lvgl/src/extra/widgets/keyboard/lv_keyboard.c`) is:

```c
else if(strcmp(txt, LV_SYMBOL_OK) == 0) {
    lv_res_t res = lv_event_send(obj, LV_EVENT_READY, NULL);
    if(res != LV_RES_OK) return;          /* <-- we deleted obj, so we exit here */
    if(keyboard->ta) {
        res = lv_event_send(keyboard->ta, LV_EVENT_READY, NULL);
        ...
    }
}
```

Deleting the keyboard during its own `LV_EVENT_READY` makes `lv_event_send`
return `LV_RES_INV`, so the second send — the one that reaches the textarea —
never happens. `DevTesterApp::field_event` (`dev_tester_app.cpp:124`) listens for
exactly that `LV_EVENT_READY` on the field, so `update_output()` is dead code
today. `LV_EVENT_CANCEL` has the identical structure and the identical bug.

**Fix.** Defer the teardown out of the event dispatch. Keep the handler
registration as it is; only the body changes.

```c
// Deleting the keyboard inside its own READY/CANCEL makes lv_event_send return
// LV_RES_INV, and LVGL then skips the matching send to the textarea. Field-level
// listeners depend on that second send, so the delete has to happen after the
// dispatch unwinds.
void keyboard_close_async(void *)
{
    crystal_keyboard_hide();
}

void keyboard_close_event(lv_event_t *)
{
    lv_async_call(keyboard_close_async, nullptr);
}
```

**Invariants to preserve.**

- `crystal_keyboard_hide()` must stay idempotent — `s_hiding` already guards
  re-entry, and an async call can now land after a synchronous hide from another
  path (Cancel button, `wifi_close_credentials()`, `onDestroy()`). The early
  `if (s_hiding) return;` plus the `s_keyboard != nullptr` checks cover this, but
  do not remove them.
- A queued async call must not outlive the keyboard's owner. `crystal_keyboard_hide()`
  runs to completion and nulls `s_keyboard`, `s_field`, `s_viewport`, so a late
  async hide is a no-op rather than a use-after-free. Verify by opening the
  dialog, pressing Done, and immediately pressing Cancel.

**Exit.** Press Done in Dev Tester's bottom field: the output label shows
`Committed / Top: … / Bottom: …` and the keyboard closes in the same frame pair.

---

## 2. Back must close one layer, not the app underneath

**Symptom.** The Back gesture over the WiFi password dialog closes Dev Tester's
keyboard correctly, but over Clock, Weather, or Calculator it closes the app.

**Cause.** `DevTesterApp::onBack()` (`dev_tester_app.cpp:161`) is the only
override in the tree:

```cpp
bool DevTesterApp::onBack()
{
    if (crystal_keyboard_is_open()) { crystal_keyboard_hide(); return true; }
    return notifyCoreClosed();
}
```

Every other app inherits `CrystalApp::onBack()` (`crystal_app.hpp:67`), which is
`return notifyCoreClosed();` — close the app, unconditionally. Brookesia maps an
edge-horizontal swipe straight to `active_app->back()`
(`esp_brookesia_phone_manager.cpp:598`), and the shell arbiter deliberately hands
Back to the app whenever a keyboard or modal is up (`crystal_shell.cpp:1206`,
`1219`). So the shell yields, and the app has no idea a keyboard is over it.

**What "consume" means here.** Consume = handle the gesture at the shell layer
and return `true` from `onBack()` without calling `notifyCoreClosed()`.
Brookesia only tears the app down when the app itself calls `notifyCoreClosed()`;
a bare `true` just means "handled, nothing further". So one Back dismisses
exactly one layer and the app keeps running. **It does not close everything and
it does not jump to the launcher.** The launcher is only reached when the app is
the topmost layer, which is the pre-Phase-10 behaviour and stays unchanged.

**Back stack, topmost first.**

| Layer | Test | Action |
| --- | --- | --- |
| Keyboard | `crystal_keyboard_is_open()` | `crystal_keyboard_hide()` |
| WiFi credentials dialog | `s_wifi_dialog != nullptr` | `wifi_close_credentials()` |
| WiFi page | `s_wifi_page != nullptr` | `wifi_page_close()` |
| Quick Settings | `s_quick_settings_open` | close panel |
| App | — | `notifyCoreClosed()` |

**Why the fix cannot live purely in the shell.** Brookesia's
`_flags.enable_gesture_navigation_back` is private with no public setter
(`esp_brookesia_phone_manager.hpp:77`), and its release callback is registered in
`begin()` — before `crystal_shell_init()` runs. The shell cannot suppress or
outrun that dispatch. The interception point has to be `onBack()`.

**Why it cannot live in each app either.** `crystal_app` does not and must not
depend on `crystal_shell` — `crystal_shell/CMakeLists.txt` already REQUIREs
`crystal_app`, so the reverse edge is a cycle.

**Fix.** One hook owned by `crystal_app`, registered by the shell at init. The
base `onBack()` consults it; no app override is needed.

In `crystal_app.hpp`:

```cpp
// Returns true when the shell dismissed a layer of its own (keyboard, modal,
// page, panel) and the app should stay open. crystal_app cannot call into
// crystal_shell directly -- the shell already depends on crystal_app -- so the
// shell installs this at init.
using crystal_shell_back_hook_t = bool (*)();
void crystal_app_set_shell_back_hook(crystal_shell_back_hook_t hook);
```

In `crystal_app.cpp`, the base implementation moves out of the header:

```cpp
bool CrystalApp::onBack()
{
    if (s_shell_back_hook != nullptr && s_shell_back_hook()) return true;
    return notifyCoreClosed();
}
```

In `crystal_shell.cpp`, the hook walks the stack top-down and returns after the
first hit:

```cpp
// One Back dismisses one layer. Order is topmost-first; returning false means
// the app is the topmost layer and Back belongs to it.
bool shell_consume_back()
{
    if (crystal_keyboard_is_open()) { crystal_keyboard_hide(); return true; }
    if (s_wifi_dialog != nullptr)   { wifi_close_credentials(); return true; }
    if (s_wifi_page != nullptr)     { wifi_page_close(); return true; }
    if (s_quick_settings_open)      { close_quick_settings(); return true; }
    return false;
}
```

Register it in `crystal_shell_init()` alongside the gesture callbacks.

**Then delete `DevTesterApp::onBack()`** and its declaration in
`dev_tester_app.hpp:22`. It becomes a redundant special case, and leaving it in
place is how this class of bug survived — one app behaving correctly hides that
the other three do not.

**Invariants to preserve.**

- Ordering is load-bearing. The keyboard is checked before the dialog because
  the dialog owns the keyboard; closing the dialog first destroys the field and
  fires `watched_object_deleted`, collapsing two Back presses into one.
- `wifi_close_credentials()` and `wifi_page_close()` both already clear the
  keyboard state callback before hiding, so calling them from the hook needs no
  extra teardown.
- The arbiter at `crystal_shell.cpp:1206`/`1219` keeps assigning
  `CrystalGestureOwner::App` for Back. That is still correct — the shell is not
  claiming the gesture, it is answering the app's question about it.

**Exit.** Open Quick Settings → WiFi → a network → tap the password field, on
each of Clock, Weather, and Calculator. Four Back gestures give: keyboard closes,
dialog closes, WiFi page closes, Quick Settings closes — and the app is still on
screen. A fifth Back closes the app.

---

## 3. The dialog lift must be measured, not hardcoded

**Decision: option B.** Lift by the measured shortfall of the whole dialog
against the reserved band, and do not move at all when nothing is covered. The
resting position stays centred. Option A — abandoning centring for a fixed high
resting position — was considered and rejected; the reasoning is at the end of
this section.

**Symptom.** The dialog always jumps up when the keyboard appears.

**Cause.** `wifi_dialog_place()` (`crystal_shell.cpp:1341`) takes the minimum
unconditionally:

```cpp
const lv_coord_t top = keyboard_open ? LV_MIN(raised_top, centred_top) : centred_top;
```

On the 480x480 panel: `raised_top = 280 − 20 − 200 = 60`, `centred_top =
(480 − 200) / 2 = 140`. So the keyboard-open case is always 60 — an
unconditional 80 px lift. The field sits at dialog-relative y 76..120, i.e.
absolute 216..260 when centred, which is already clear of the 280 px keyboard
top. `DESIGN.md:501` is explicit: *"Focused field already fully visible → do not
move it. Even if the keyboard is nowhere near it. No jump."*

**Why the field alone is the wrong thing to measure.** The Connect and Cancel
buttons are bottom-aligned at `-14` with height 38, so centred they occupy
absolute 324..362 — under the 280 px keyboard top. The lift is what currently
keeps them reachable. Gating it on field overlap alone would satisfy the letter
of §7 and leave the buttons buried, trading a spec bug for a usability bug. The
thing that must clear the band is the whole dialog, not the focused field.

**Fix.** Replace the unconditional `LV_MIN` with a measured shortfall. The
dialog height is a known constant, so this is arithmetic — no layout probing.

```cpp
// Lift only by what the band actually covers, and only when it covers something.
// Measured against the same reserved_top that crystal_keyboard_show() uses for
// its viewport-shrink test, so the two never disagree about what is covered.
const lv_coord_t centred_top = static_cast<lv_coord_t>((lv_disp_get_ver_res(nullptr) -
                               kWifiDialogHeight) / 2);
lv_coord_t top = centred_top;
if (keyboard_open) {
    const lv_coord_t overlap = static_cast<lv_coord_t>(centred_top + kWifiDialogHeight +
                               kWifiDialogGap - crystal_keyboard_reserved_top());
    if (overlap > 0) top = static_cast<lv_coord_t>(top - overlap);
}
```

`raised_top` is no longer needed and should be removed with it.

### Three hazards, all cheap to handle

**1. Do not measure children.** `wifi_dialog_place()` is called at `:1379`,
immediately after `lv_obj_set_size` and *before* the title, network label, field,
and buttons are created. `lv_obj_get_coords` on a child would read garbage on
that first call and something different on the later state-callback call, giving
two different placements for the same dialog. Use `kWifiDialogHeight`. If the
dialog ever gains a content-driven height, the constant has to be replaced by an
explicit measurement *after* population — not by probing children here.

**2. Measure the whole dialog against `crystal_keyboard_reserved_top()`, not the
field.** The dialog is also the keyboard's viewport —
`crystal_keyboard_show(input, s_wifi_dialog)` at `:1460`. `crystal_keyboard_show()`
shrinks that viewport whenever `viewport_area.y2 >= keyboard_top`
(`crystal_keyboard.cpp:186`). A lift sized only to clear the *field* still trips
that test: the dialog gets height-clipped, made scrollable, and the buttons end
up behind a scroll instead of on screen. Sizing the lift to the full dialog plus
`kWifiDialogGap` keeps both calculations agreeing on what is covered. With
today's numbers the lift is 80, landing the bottom at 260 — clear of 280, so the
shrink is correctly skipped.

**3. The existing clamp can silently eat the lift.** The tail of the function is
`LV_MAX(0, top - parent_area.y1)`. If a future taller dialog needs more headroom
than exists above it, that clamp quietly applies a *partial* lift and the buttons
are covered again with no diagnostic. Clamp explicitly and log when the requested
lift cannot be honoured, rather than letting `LV_MAX` absorb it.

Keep measuring the parent origin (`parent_area.y1`) rather than assuming it: the
parent starts below the status bar, and `LV_ALIGN_TOP_MID` offsets are
parent-relative. Recall `visual-area-is-display-coords` — mixing display and
parent coordinates here is how the dialog ends up off by the status-bar height.

### What B does and does not change today

For the current 200 px dialog, B computes a lift to top = 60 — **exactly what
the code already does**. B is behaviour-preserving on this geometry, so it will
not stop the jump described in the symptom: with the buttons at 324..362 that
lift is legitimate, and the reported "unnecessary" motion is correct behaviour
once the buttons are counted. What B buys is that the rule becomes a measured
condition instead of a constant — it holds still automatically for any dialog
that does not need the lift, and it stays correct if this dialog is ever
reshaped. That generality is the reason for choosing it.

**Option A, for the record.** A cannot be done by shrinking: a centred dialog
fits above the band only if `(480 − h) / 2 + h <= 280`, i.e. `h <= 80` — far
short of title, network label, 44 px field row, and 38 px buttons. A therefore
means abandoning centring for a fixed high resting position (~80), which changes
the no-keyboard appearance on every open, leaves the neighbouring
forget-network box centred at 350x150 (`:1467`) resting at a different height,
and makes both the `keyboard_open` parameter and the
`crystal_keyboard_set_state_cb` registration (`:1459`) vestigial — pulling the
keyboard state-callback API into the change. Fewer edited lines, wider blast
radius.

**Exit.** With the keyboard up, the password field is fully visible and the
Connect and Cancel buttons are tappable, with no scroll inside the dialog. The
dialog does not move when nothing would be covered — verify by temporarily
reducing `kWifiDialogHeight` so the centred dialog clears the band, confirming
zero motion, then restoring it.

---

## 4. Quick Settings must cover the keyboard, not fight it

**Symptom.** The pull-down does nothing while the keyboard is up.

**Cause.** Two independent blocks.

- Gesture: `crystal_shell.cpp:1219` tests `s_keyboard_open` in the same
  `else if` chain that would otherwise evaluate the top-corner pull, and assigns
  `CrystalGestureOwner::App`. The pull-down branch at `:1234` is unreachable
  while a keyboard is open.
- Z-order: `s_quick_root` is created on `lv_layer_top()`
  (`crystal_shell.cpp:228`) and so is the keyboard (`crystal_keyboard.cpp:213`).
  The keyboard is created later, so it is the later sibling and draws on top.
  Even with the gesture armed, the panel would slide in behind the keyboard.

`DESIGN.md:508` is the target: *"The pull-down covers the keyboard rather than
closing it."*

**Fix.** Both parts, in this order.

1. Let the pull-down arm while the keyboard is open: `s_keyboard_open` should no
   longer force `App` ownership before the top-corner test runs. Keep
   `s_settings_open`, `s_switching`, and the card-transition guards as they are —
   only the keyboard clause moves.
2. Raise the panel above the keyboard when it opens:
   `lv_obj_move_foreground(s_quick_root)` in `create_quick_settings()`, after the
   root exists. Do not reparent the keyboard and do not hide it — the keyboard
   stays open underneath, which is the whole point of the rule.

**Invariants to preserve.**

- The keyboard must not close. `crystal_shell_set_keyboard_open()` state and the
  reduced viewport both stay as they are while the panel is over it.
- Dismissing Quick Settings must reveal the keyboard still up, with the caret and
  typed text intact. The panel's own teardown must not call
  `crystal_keyboard_hide()`.
- `s_page_dots` (`:367`) is also on `lv_layer_top()`. Confirm the new foreground
  order does not push the dots over the panel.

**Exit.** With the keyboard up, pull down from the top-right corner: the panel
slides over the keyboard, the keyboard is visible underneath at the edges and is
not dismissed. Dismiss the panel: the keyboard is still up and still accepts
keys.

---

## 5. Order of work

1. §1 — self-contained, and §2's exit test needs Done to work.
2. §2 — delete `DevTesterApp::onBack()` in the same change.
3. §4 — gesture arbiter and z-order.
4. §3 — option B, decided. Behaviour-preserving on the current geometry, so it
   can land last without blocking the others.
5. §6 — record on device, then close the phase.

§2 and §4 both edit the arbiter chain around `crystal_shell.cpp:1206`..`1236`.
Doing them in separate commits keeps the diff readable; doing them at the same
time avoids resolving that chain twice.

---

## 6. Device validation — required before Phase 10 closes

A green build proves compilation, nothing else.
`IMPLEMENTATION_PLAN.md:677` still reads *"pending hardware validation"*, and
`VALIDATION_CHECKLIST.md` has no Phase 10 section — its only keyboard line
(`:232`) merely asserts the lock APIs exist.

Add these to `docs/VALIDATION_CHECKLIST.md` as a Phase 10 block, unchecked, and
tick them on hardware:

- [ ] Every key is comfortably tappable. At ~5.6 px/mm the keys are roughly
  7x8 mm; check the outer columns and the 4-row bottom keys specifically.
- [ ] Typing near the bottom row does not trigger Brookesia's bottom navigation
  gesture. The 26 px `kBottomSafe` inset is the whole defence here.
- [ ] A covered field animates to the midpoint in ~250 ms, ease-out, once — no
  double-scroll, no overshoot.
- [ ] A field that is already fully visible does not move at all.
- [ ] The WiFi dialog lifts only as far as the band requires, and the Connect and
  Cancel buttons are on screen with no scroll inside the dialog (§3 exit).
- [ ] Open and close the keyboard ten times from the WiFi dialog: no orphaned
  keyboard, no dimmed background left behind, viewport height and scrollability
  restored each time.
- [ ] Plane switching (abc / ABC / 123 / #+=) never moves delete, space, cursor
  arrows, or Done.
- [ ] Back over Clock, Weather, and Calculator dismisses one layer per press and
  leaves the app running (§2 exit).
- [ ] Quick Settings pulls down over an open keyboard and the keyboard survives
  the dismissal (§4 exit).

Before trusting any of the visual results, confirm the generated `sdkconfig`
actually carries the display options you think it does — see
`waveshare-display-config-trap`. A BSP option silently dropped by Kconfig will
make a tearing or timing observation meaningless.

---

## 7. What this hot fix does not change

- The 200 px reserved band and the published keyboard top. Both are stable and
  other layout code depends on them.
- The iOS-derived key maps, the four planes, and the fixed control-key geometry.
- `crystal_keyboard_show()`'s viewport reduction, including the guard that
  refuses to shrink a viewport the keyboard would not cover.
- The gesture arbiter's ownership model. Back still routes to the app; the app
  now asks the shell first.
