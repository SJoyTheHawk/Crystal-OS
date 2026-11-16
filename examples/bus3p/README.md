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
verification, including the earlier 870–919 ms startup warning. The 512 KiB
quota is unchanged. Install /tmp/phase17-bus-stagec.capp through the Control
Panel after uninstalling the previous version; no firmware flash is needed.

## Stage C memory correction (0.2.4)

The catalog-enabled package uses a provisional 512 KiB Lua arena. The old
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
lookup, provider-backed clock-time arrivals, joint-provider ETA merging and
automatic refresh remain Stage D or later work.
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
