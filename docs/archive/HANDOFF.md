# Handoff — lifecycle change, 2026-09-04

## Standing statements — read before changing switching or lifecycle

Two things are settled by the owner and are not open for a session to re-decide.

**1. The 50% visual crossover is required.** Card switching is a finger-tracked
preview that follows the drag from the edge, with 50% as the commit threshold:
release past it completes to full screen, release before it returns to the origin
side. This is specified in `DESIGN.md` §5, which is the design authority.

Phase 6 shipped a snap-and-fade transition and Phase 6.5 measured 91-106 ms render
time against a 12 FPS gate. That defers the crossover to Phase 7.5; it does **not**
replace it. Where a document says the crossover "is not used", it is describing an
interim fallback. A failed performance gate is a bill to pay, not a design change —
the render cost is the defect to fix. Confirmed by the owner on 2026-09-04 after an
earlier session read the deferral as a cancellation.

Settled alongside it and not to be reopened: the card ring is home, and the
launcher opens from the navigation bar as overflow.

**2. Crystal OS borrows four Android lifecycle states.** `onCreate`, `onResume`,
`onPause`, `onDestroy`. Those four are dispatched and are the contract app authors
learn. `onBack` is also dispatched but is a gesture callback, not a lifecycle state.
`onStart`/`onStop` are declared for forward compatibility and are **never called in
v1** — nothing that must run may live in them. There are no install/uninstall hooks.

## Status — closed

Closed on 2026-09-04. Clock's former install work now runs once behind an
instance guard in `onCreate()`. Active ABI and example references to
`onInstall()`/`onUninstall()` were removed, and the lifecycle, card navigation,
keyboard gesture lock, battery charging state, and shared-I2C polling contracts
were synchronized across the design documents. Countdown timers reset after a
reboot; the stopwatch survives. The optional future `summary()` hook remains an
open design question and is not part of ABI v1 today.

The source and documentation checks pass. An ESP-IDF 6.1 build was attempted,
but the sandbox denied the component manager's macOS `sysctl()` process query;
the request to run it outside the sandbox was rejected before execution. No
firmware was flashed. The sections below are retained as the historical handoff
context and may describe items that are now resolved.

## ⚠️ The tree does not compile

`onInstall()` was removed from `CrystalApp`, but `ClockApp` still overrides it.
Fix this first — it is two deletions:

- `components/clock_app/include/clock_app.hpp:16` — remove `bool onInstall() override;`
- `components/clock_app/src/clock_app.cpp:41-52` — remove the `ClockApp::onInstall()` body

**Do not simply delete the body's contents.** It does real work that has to land
somewhere, described under "Clock's boot reconciliation" below.

## What changed

Two files, both in `components/crystal_app`.

### `include/crystal_app.hpp`

Removed `virtual bool onInstall()` and `virtual bool onUninstall()`. Six hooks
remain declared, of which **four are dispatched** — the four borrowed Android
lifecycle states `onCreate`, `onResume`, `onPause`, `onDestroy` — plus `onBack`, a
gesture callback. `onStart`/`onStop` are declared and overridable but not called in
v1.

### `src/crystal_app.cpp`

`init()` and `deinit()` are now lifecycle bookkeeping only — they set
`lifecycle_state_` and return `true`, forwarding to no app hook. Previously they
called `onInstall()`/`onUninstall()` and logged them.

Side effects worth knowing:

- `init()` no longer has a failure path. It previously returned `onInstall()`'s
  result, so an app could refuse to install. Nothing used that.
- The `"%s lifecycle: onInstall"` log line is gone. `VALIDATION_CHECKLIST.md:122`
  asserts that line appears once per app at boot — **that check is now invalid**
  and needs rewriting or removing.

## Why

`onInstall()` does not exist in Android's lifecycle, and the name misdescribed
what the only implementation actually did. `ClockApp::onInstall()` was not
installing anything — it was reconciling **timer service** state at boot: reset
the timer, clear its NVS keys, sync the stopwatch flag into `crystal_core`.

Once-per-boot setup now belongs in `onCreate()`, guarded by the app when it must
not repeat.

## Clock's boot reconciliation — unresolved

This is the one open item, and it needs a decision rather than a mechanical edit.

The old `onInstall()` body did three things:

```cpp
crystal_timer_reset();          // clear service state
state().erase("tm_end");        // clear persisted timer keys
state().erase("tm_ps");
state().erase("tm_dur");
uint32_t started = 0;
state().get_u32("sw_st", &started);
crystal_stopwatch_set_running(started != 0);   // restore stopwatch indicator
```

It cannot move verbatim into `onCreate()`. `onCreate()` fires on **every** launch,
so clearing `tm_*` there would wipe a running timer every time the user opens
Clock.

It also cannot move wholesale into `crystal_core_init()` — I tried this and
reverted it. The `tm_*` keys live in **Clock's** `CrystalState` namespace, so the
service cannot erase them without reaching across the namespace boundary that
`APP_PLATFORM.md` relies on for sandboxing.

Two workable options:

**A. Guard it in `onCreate()`.** Add a `bool boot_setup_done_` member; run the
block once and skip thereafter.

```cpp
bool ClockApp::onCreate()
{
    if (!boot_setup_done_) {
        boot_setup_done_ = true;
        // ... old onInstall() body ...
    }
    // ... existing onCreate() ...
}
```

Smallest change, keeps Clock's NVS keys inside Clock. The member is per-instance,
and the instance outlives teardown, so "once per boot" holds.

**B. Split ownership.** `crystal_core_init()` calls `crystal_timer_reset()`
(service state, service's job); Clock clears its own `tm_*` keys under a guard in
`onCreate()`. Cleaner conceptually, two places to look instead of one.

I lean A for now and B when the timer service grows its own persistence.

Note on behaviour either way: the stopwatch indicator glyph currently restores at
boot, before Clock is ever opened. Under A it still does, because `onInstall()`
and the guarded `onCreate()` both run during the boot install pass. Worth a
deliberate check — whether a stopwatch should survive a reboot at all is a product
question nobody has answered.

## Docs updated

- `DESIGN.md` §5.5 — lifecycle table corrected to the **shipped** semantics
  (`onCreate` every launch, `onPause` saves state, `onStart`/`onStop` reserved),
  plus the note that `onDestroy()` does not free the instance.
- `IMPLEMENTATION_PLAN.md` §3 — gates G3 and G4 closed.

## Docs still stale

- `CODE_GUIDE.md:277, 419, 422, 426` — still documents `onInstall()`, including
  the class listing and "where Clock registers its expiry with `crystal_service`".
- `IMPLEMENTATION_PLAN.md:230, 284` — Phase 4.5 text describes sealing
  `init()`/`deinit()` **as** `onInstall()`/`onUninstall()`, now the opposite of
  what the code does.
- `VALIDATION_CHECKLIST.md:122` — asserts a log line that no longer exists.
- ~~`APP_PLATFORM.md:240` — lists `onInstall` in the ABI hook set.~~ **Resolved
  2026-09-04.** §6.6 now states the four dispatched lifecycle states
  (`onCreate`/`onResume`/`onPause`/`onDestroy`), marks `onBack` as a gesture
  callback, and marks `onStart`/`onStop` as declared-but-not-dispatched. `onInstall`
  is gone from the hook set. The false "`onStop` fires when an OS overlay covers the
  app" claim was corrected there and in `examples/clock3p/README.md` — both told app
  authors to put timer teardown in a hook that never runs.
- `examples/clock3p/main.lua:122` — defines `M.onInstall()`, which the host will
  no longer call.

## Design decisions from this session, not yet all written down

Settled and in the docs:

- Cards are home; no launcher in the common case (`DESIGN.md` §5.5)
- Boot to last-viewed card, fallback slot 0
- Fixed slot order, **no wrap**
- Page dots in the indicator bar
- Launcher demoted to overflow past ~6 apps
- Settings is a screen override at layer 5, with two-level back
- Gates G3 (480×480 IPS/ST7701) and G4 (AXP2101 fuel gauge) closed

Settled, **not yet written**:

- Drop the proposed `claimsEdgeGestures()` opt-out. Horizontal belongs to the OS,
  absolutely. Rationale: an opt-out in a versioned ABI is a permanent commitment,
  and reserving the axis now is the forward-compatible direction — v2 can grant
  it, but revoking it would break every app that used it.
- No recents preview. A chooser for 3-5 fixed cards is overhead.
- Suppress card switching while the keyboard is open. Text entry is committed
  activity; an accidental swipe mid-sentence reads as data loss.
- Battery icon needs a **charging** state — the AXP2101 gives `isCharging()` for
  free, so it costs nothing.
- I2C is a shared bus (GT911, PCF85063, AXP2101, QMI8658, ES8311, ES7210). Poll
  battery no more than every 30s and the RTC only at boot plus after SNTP, or
  touch latency suffers and shows up as drag jitter.

Open, needs your call:

- Reserve a `summary()` hook in ABI v1 for a future dashboard card? Same
  asymmetry argument as the horizontal axis: free now, expensive after ship.
- Does a running stopwatch survive a reboot? (see above)

## Not verified

- The build. No ESP-IDF on this machine (`idf.py` not found, `IDF_PATH` empty), so
  nothing here has been compiled. The `onInstall()` breakage was found by reading,
  not by building — there may be more.
- Brookesia's own source. `managed_components/` is unpopulated in both reference
  trees, so claims about `installApp()` registering launcher icons and
  `notifyCoreClosed()` returning to the launcher are inference from the demo apps,
  not from its headers. This matters for the "launcher as overflow" plan: if the
  launcher is wired into the lifecycle, leave it intact and simply never navigate
  to it rather than patching upstream.
