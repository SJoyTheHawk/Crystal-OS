# Bus Lite live-provider package (0.2.29)

Version 0.2.29 subscribes to OS network readiness. Favorites and provider requests
wait for Wi-Fi (or Ethernet) to acquire an IP address. Disconnect cancels current
work, suppresses late callbacks and shows **No connection**; reconnect refreshes
the visible page automatically. Bundled route search still works offline.
Network readiness does not guarantee that DNS or the provider is reachable.
Flash the matching firmware with **app-flash**, then install the signed package
through Developer Tools. The new net subscription bindings are required.

Version 0.2.26 subscribes to Crystal OS clock readiness. Before RTC, SNTP or
manual time initialization completes, arrival fields display **--** with
**Waiting for device time**, rather than subtracting boot time from a real ETA.
The ready notification immediately redraws cached arrivals; Favorites also
recalculate displayed minutes between network refreshes. Notifications defer
while paused and subscriptions are released on destruction. This version needs
firmware containing the new time subscription bindings: flash it with
**app-flash** before installing the package. Older firmware reports an explicit
clock-subscription-unavailable error. The current uninstall-before-replacement
flow deletes saved favorites; installed data is retained by firmware app-flash.

Version 0.2.25 saves the resolved ETA URLs and operator-specific stop IDs with
favorites. Joint favorites refresh KMB and CTB directly, sequentially, and merge
their arrivals; a failure from one provider leaves the other provider's times
visible. Matching happens before saving a new joint favorite. If matching has
not finished, Save stop asks you to wait or retry. Older joint favorites resolve
and persist their missing endpoints once on their next successful Favorites
refresh. Failed storage writes remain retryable. URL validation runs on load,
not during each refresh. This update needs only the rebuilt app package.

Version 0.2.24 adds the Phase 4 ETA scheduler. The shared 100 ms timer updates
visible countdowns, refreshes the selected stop every 20 seconds, and refreshes
saved Favorites sequentially every 30 seconds. Each favorite card shows up to
three live times and never persists a countdown. Navigation and pause invalidate
requests through the existing generation guard. Version 0.2.23 adds the
three-column Favorites layout, standard-service route
catalogs and icon tabs with full-width touch targets. The icon, label and
surrounding tab space all select the tab. This uses the existing app ABI;
install the rebuilt package through the control panel without a firmware flash.
Phase 3 persistence is accepted; P4 board acceptance remains.

Version 0.2.20 enlarged route labels to 28 px and added a **Recent:** heading
above recent searches. Version 0.2.21 tightens the detail page's top spacing and
header height, then gives stop rows content-sized heights with a visible gap so
long names wrap while short rows stay compact. Version 0.2.22 gives the detail
header, stop list, destination and ETA controls the same 16 px scaled side
padding as Favorites and Search. The stop list now fills the remaining viewport
down to its bottom status line. It does not require a new firmware API.

Version 0.2.19 packs possible alphabetic route keys into consecutive rows of
two columns. Unavailable letters remain hidden; reusable key slots update their
labels and selected letter with the current route prefix. The production LVGL
host test checks keypad positions and key dispatch at 480 px and 720 px.

Version 0.2.18 hardens favorites and recent-route persistence. Stored records
are validated on load; invalid entries are skipped with a visible notice.
Saving, removing, or recording a route updates the live list only after
`crystal.state.set()` succeeds, so a failed write does not discard the current
data. The saved-stop layout and interaction remain unchanged.

Version 0.2.17 removes the duplicate guidance above route results and the
stop list. Loading, instructions, results and errors remain in the bottom
status field. Favorites, Search, stops and arrivals use the same 20 px status
font, one step larger than the previous 16 px font. This is a package-only
update for the current firmware.

Unresolved stop rows show **Loading...** until their names are available.
Resolved rows retain their sequence number and name. Version 0.2.16 removes
per-tick pacing from catalog preload, route-row publication, response decoding,
stop-list creation and bundled metadata resolution. Each callback finishes
available local work until it needs a provider response. The 200 ms typing
debounce and selected 5/30-second Lua deadline remain. Install the rebuilt CAPP;
no additional firmware flash is required on the latest runtime.

Current Lua quota: **4 MiB (4096 KiB)**, set on 2026-11-17. Each search scans the full resident route catalog in one callback; only the eight visible results are expanded. Version 0.2.16 also publishes all visible route rows together.

The firmware adds **Settings → Developer Tools → Extended Lua Timeout**: off is
5 seconds (default), on is 30 seconds per Lua callback. It is saved across
reboots and applies from the next invocation, including script entry loading.
Flash the rebuilt firmware with idf.py app-flash, then install the new CAPP.
The independent ESP-IDF task watchdog keeps its existing configuration; a long
callback can still stall the UI and cause task-watchdog reports.

On its first scheduler callback the app loads all 2,115 bundled route records into Lua
memory as compact page strings; only visible results expand into tables.
The search keypad stays disabled until loading completes. Numeric keys remain
visible and disable when unavailable; alphabetic keys hide unless they continue
a valid route prefix, then reappear when they become possible. Detailed stop metadata remains
loaded on demand. The keypad prefix map is built on the host and retained as
a 2.6 KiB string; startup no longer builds thousands of prefix keys in Lua.
Host production-binding tests peak at about 0.7 MiB of the
4 MiB arena; P4 startup time and resource headroom still require board testing.

Phase 1 and Phase 2 use the official KMB/LWB and Citybus HTTPS APIs for complete stop
sequences and ETA. Bundled catalogs provide offline route search and stop names;
missing stop details are fetched sequentially. IDs remain strings, and repeated
stop occurrences keep their sequence numbers. The accepted Favorites/Search,
stop-picker and ETA layout is retained.

Install **`/tmp/phase17-bus-live.capp`** through the P4 Developer Tools Control
Panel after updating the firmware with `app-flash`. The new firmware frees
native JSON trees even when Lua conversion runs out of memory; OS stays
0.17.0. It also provides the shared selection callback and in-place row update
used by the continuous stop list. Uninstall the previous package before replacing it (saved data is
removed). The package is signed with the existing test key, ID 240. No Mac
server is required.

Follow [BUS_APP_QUICK_START.md](../../docs/bus/BUS_APP_QUICK_START.md) for the full
step-by-step flash/install/board test. Host checks and the firmware build pass;
board TLS, timing, resource headroom and touch acceptance remain pending.
[BUS_APP_PHASE_1_RESULTS.md](../../docs/archive/bus/BUS_APP_PHASE_1_RESULTS.md) records the
artifact hashes, official response provenance and separate host/P4 evidence.

Joint-provider ETA merging is implemented in Phase 2. A JOINT catalog route
uses the KMB sequence, matches the nearest CTB stop using the Flutter
coordinate rule, then fetches and merges both providers sequentially. A failed
source leaves successful arrivals visible with a partial-data status.
Phase 3 persistence hardening is implemented in 0.2.18; the user reports its P4
save/reopen/reboot/remove, recents, joint favorite and limit checklist passed on
2026-11-18. Phase 4 scheduled refresh/countdown remains open. Existing manual
refresh, saved-stop reopening and recent routes are retained.
The final functional release remains 0.3.0 after acceptance.

The host fixture tool can still produce an explicitly named **Bus Lite
(fixture)** package using provider-shaped synthetic responses. This is a test
option; production packages point directly at official HTTPS hosts. Historical
Stage C notes below describe the earlier fixture milestone.

---

The previous stop-picker instructions below describe the fixture-era page-based
UI and are retained as historical notes. Current stop-list behavior and live
package testing are in the linked Phase 1 quick start.

## Stage C callback correction (0.2.5)

The 0.2.4 P4 retest passed launch with 178 KiB peak Lua payload, but catalog
search exceeded the unchanged 50 ms callback limit. Version 0.2.5 separates
file reads from JSON conversion: each 100 ms timer tick reads one page OR
converts at most one needed record. At most 16 cheap line checks run per tick.
Route pages are filtered by actual route initials; stop pages by provider ID
ranges. Search retains eight results and full match counts. The compact
runtime.txt directory avoids decoding provenance/index JSON on launch.

Host tests enforce those per-tick work bounds and verify real catalog paging,
repeated searches, metadata, cancellation and long stops. P4 timing still needs
verification, including the earlier 870–919 ms startup warning. At that
historical milestone the 512 KiB quota was unchanged. Install
/tmp/phase17-bus-stagec.capp through the Control
Panel after uninstalling the previous version; no firmware flash is needed.

## Stage C memory correction (0.2.4)

At that historical milestone, the catalog-enabled package used a provisional
512 KiB Lua arena. The old
160 KiB fixture quota failed during P4 launch on 2026-11-16. Search now keeps
only eight matching records and rescans the sorted catalog for another page;
all matches remain reachable. Only index filenames and keypad letters stay
resident, rather than the full provenance manifest and descriptor tables.

Host coverage now loads actual assets with production JSON conversion, Lua
allocator and UI bindings. P4 measurements and acceptance remain required.
Install the new signed CAPP through Developer Tools > Control Panel after
uninstalling the previous Bus Lite package (this removes its saved data).
No firmware flash is needed for this app-only correction.

# Bus Lite package (0.2.5)

The package follows the extracted native Bus layout in
`examples/bus_app/components/bus_app/src/bus_app.cpp`: Favorites/Search tabs,
slate cards, large route display, numeric keypad with a scrolling letter bank,
route/destination results, and a dedicated Back/stop-picker page and separate ETA board. Controls scale
to the P4 display. Selecting a stop loads arrivals; Save stop and Remove manage
actual stop favorites. The empty search field shows up to five recently opened
routes, saved locally and available without a network request.

Route discovery now uses the bundled Stage C catalog under `assets/catalog/`:
normalized KMB/CTB/joint route pages are read a page at a time, prefix matches
are naturally sorted, and eight reusable result rows expose Previous/Next
coverage. The selected route still uses the deterministic Mac fixture for its
ordered stop sequence and arrivals in this milestone, so those values remain
explicitly labelled as fixture data. Stop metadata is filled from the bundled
operator catalog when the fixture omits it. The app keeps complete route and
stop lists, three arrival rows and eight saved stops. Controls are reused and
the production path uses the 64-callback runtime limit.

The catalog was generated from the pinned 2026-10-02 recorded routeFareList
and KMB stop snapshots. Its manifest records source hashes and gaps. Rebuild it
with `tools/build_phase17_bus_catalog.py`; the generator rejects any file over
32 KiB, more than 128 files, or more than 5.5 MiB of catalog output.

## Install

**Update the OS firmware first** using the newly built `build/crystal_os.bin`.
OS version stays 0.17.0; an older binary with the same version is insufficient.
The corrected Lua list binding preserves the item argument and avoids an
unsigned sentinel overflowing 32-bit Lua integers. This fixes empty Stops/ETA
lists despite successful HTTP. Version 0.2.2 also uses the new `bg_opa` style.
An app-only flash/OTA preserves the installed-app partition.

Restart the Mac fixture server to pick up stop-specific responses:

```sh
python3 tools/phase17_bus_fixture.py serve
```

In another terminal:

```sh
MAC_IP=192.168.1.195
PACKAGE_KEY="$HOME/.crystal_keys/phase17-test-ed25519.pem"
KEY_ID=240
python3 tools/phase17_bus_fixture.py create --host "$MAC_IP"
python3 tools/crystal.py pack /tmp/phase17-bus --key "$PACKAGE_KEY" --key-id "$KEY_ID" -o /tmp/phase17-bus.capp
python3 tools/crystal.py inspect /tmp/phase17-bus.capp
```

Use Python with `tools/requirements-pack.txt` installed. Uninstall old Bus Lite
in Manage Apps, upload `/tmp/phase17-bus.capp` through the **P4** web portal, and
apply/restart when prompted. Confirm package version **0.2.5**. Keep the Mac
server running. Uninstalling removes the old app's saved data.

## Device checks

1. Launch. Favorites/Search occupy the top row. A fresh install shows No saved
   stops yet below them, without overlapping text.
2. Open Search and type `1A` on the keypad. The letter bank scrolls separately.
   The catalog loads matching route cards such as 1A KMB and a `To <destination>`
   description. Wait for the catalog status to finish before selecting a card.
3. Clear the route field. The Recent routes list should show 1A KMB. Tap it and
   confirm the stop list loads directly; repeat the same route and make sure it
   stays a single recent entry. Leave and reopen Bus Lite to confirm it persists.
4. Tap the route card. A dedicated stop page fetches `/api/stops`, displays the
   complete ordered sequence eight rows at a time, and exposes Previous/Next
   when the route has more than eight stops. The old Live tab and Stops button
   have been replaced by this flow.
5. Tap **2. City Hall**. The ETA board opens. `/api/eta` must include
   `stop=KMB-1A-002`. Arrival rows appear below; scroll down if necessary.
6. Tap **Save stop**, **Back** to the stop picker, **Back** to Search, then **Favorites**. The saved card must show
   the route, City Hall and returned arrival summary immediately.
7. Tap that card to fetch the same stop's arrivals. Test Refresh and Remove,
   then return to Favorites and confirm removal.
8. Search `2`, choose CTB and a stop. Confirm CTB and a `CTB-2-...` stop ID
   in the request. No old KMB results should appear attached to this route.
9. Search `68X`, choose KMB and confirm the fixture's 17-stop response. Use
   Next to reach page 2 and Previous to return to page 1; the final row must be
   reachable and remain numbered 17.
10. Stop the Mac server and Refresh; confirm a visible failure. Restart and
   retry. Leave during a request and confirm no stale update on return.
11. Save a stop, leave/reopen and reboot; confirm Favorites persists.
12. Repeat launch/search/stop/leave/uninstall ten times and capture cleanup logs
    before claiming Phase 17 acceptance.

## Host checks

```sh
python3 tools/test_phase17_bus_package.py
python3 tools/test_phase17_bus_render.py
```

The render test compiles production Lua widget/list bindings, CrystalUI and real
LVGL. It checks append/insert, keypad queries, stops, selected-stop ETA requests,
favorites, failures, cancellation and repeated refresh at 480/720 pixels. PNGs
are saved under `.cache/phase17-bus-render/renders/`. HTTP and state are adapters;
these checks do not establish physical-board touch/network acceptance.

## Visual reference

Version 0.2.2 follows the native reference keypad proportions (75% screen width,
centered), flat tabs with active text and underline, slate palette and hairline
card borders. The separate stop-picker and ETA board follow the four-page
structure in `examples/bus_app/docs/bus-app-ui-design.md`. Arrival minutes lead
the board, with quieter remarks, a primary Save action and secondary Refresh.
The empty state points to Search. Status belongs to its current page.

This does not implement every native/design-document feature: nearest-stop
lookup and automatic refresh remain later work. Joint-provider ETA merging is
implemented in 0.2.9 and remains pending live P4 acceptance.
Screenshots from production UI bindings are available under
`.cache/phase17-bus-render/renders/720/`.


Version 0.2.2 restored the blank route input and native cross/backspace symbols.
Route descriptions use "To <destination>". Route descriptions and stop names
use 28-point text on P4 (20-point at 480px). The detail header uses a back symbol
and top-right refresh icon; the full-width bottom Refresh and stop-count fixture
footer are removed. Verify that clear removes the entire input, backspace removes
one character, and header Refresh reloads either stops or arrivals as appropriate.
These changes are package-only if the 0.2.1-compatible firmware is already flashed.

Version 0.2.3 adds five bounded recent routes. Opening a route moves it to the
top of the recent list; opening an existing route moves it forward instead of
duplicating it. Clear the search input to see recents, then tap one to reload its
stops. Recent routes persist in app state and require no fixture request to show.

## Memory-failure firmware retest

The 2026-11-16 runtime fix requires `app-flash`. Install Bus Lite 0.2.3 to test
recent routes. Repeatedly search, select stops, refresh arrivals, go Back and open
Favorites, then leave/reopen the app. Confirm no quota toast/reboot during normal
use and capture serial output. The host test covers 1,000 refreshes, recoverable
GC pressure, actual quota failures and full arena release; board acceptance
remains pending.
