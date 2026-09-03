# Crystal OS — App Platform (v2 track)

How third-party apps are written, packaged, installed, and contained. Extends
`DESIGN.md` and `IMPLEMENTATION_PLAN.md`; where they describe v1's
firmware-bundled catalog, this describes what replaces it.

**Status: design, not built.** Nothing here exists in the tree yet except
`examples/clock3p/`, which is a worked example written against the proposed ABI.
The example came first on purpose — designing a widget façade without a real app
to build with it produces an API nobody can use.

The v1 limit this removes is `DESIGN.md` §9's honest caveat: *users can only
install what shipped in the firmware.* Everything below exists to delete that
sentence.

## 1. Settled decisions

Closed. Restated so no phase re-opens them.

- **The ABI is the product.** Runtime tiers are swappable front-ends over one
  narrow, versioned, handle-based host API. The language is an implementation
  detail; the ABI is a compatibility promise.
- **One folder per app.** Program, assets, and state live together under
  `/apps/<id>/`. Deleting the folder is a complete uninstall.
- **Folder name is the app id, reverse-DNS.** `com.example.clock3p`. Ugly, and
  the only collision rule that survives an open catalog. Display name comes from
  the manifest.
- **No global `apps.yaml`.** Manifests are developer-authored and immutable, and
  live inside each folder. Arrangement (enabled, slot) is user-mutated and stays
  in the NVS registry from Phase 5. They are reconciled at boot (§5).
- **Authors write YAML, the device reads JSON.** Converted at pack time.
- **`.capp` is transport only.** On the device there is only ever a folder.
- **Script apps store state in `data/`, not NVS.** Otherwise a hand-deleted
  folder orphans `CrystalState` keys and clean-uninstall becomes a half-truth.
  Bundled C++ apps keep using NVS `CrystalState` unchanged.
- **Native code means trusted.** There is no MMU protection on the S3, so a bad
  native app corrupts the OS heap rather than dying cleanly. Scripts are the
  untrusted tier; native loading is for curated apps only.
- **LVGL is not exposed.** Apps see the Crystal widget façade (§6). Its C API is
  too large and its object lifetimes too pointer-shaped to survive a sandbox.

## 2. On-device layout

```
/apps/
  com.example.clock3p/
    app.json          converted manifest; read-only to the app
    main.lua          entry point named by the manifest
    assets/           icon.bin, sounds, images — read-only, mapped from flash
    data/             writable, quota'd; state.json appears on first write
  .staging/           in-flight install; pruned at boot if found
```

Bundled C++ apps have no folder. They are compiled in, and the registry treats
them as records with `origin = BUILTIN` (§5).

## 3. Manifest

`app.yaml` as authored; `app.json` as installed. The loader also accepts a
hand-written `app.json` directly, which is what makes editing a folder over USB
practical.

```yaml
abi: 1                          # host API major version; refuse to install if unsupported
id: com.example.clock3p         # folder name; unique, filesystem-safe
name: Clock                     # display name, launcher + Manage Apps
version: 1.0.0                  # semver, shown in the panel
min_os: 1.0.0
author: Example Co.
license: MIT

runtime: lua                    # lua | declarative | native
entry: main.lua
icon: assets/icon.bin           # LVGL binary image, loaded on demand
bar_style: dark                 # indicator bar contrast (DESIGN.md §2)

memory_kb: 160                  # hard ceiling; OOM kills this app, not the OS

permissions:
  storage_kb: 32
  notify: true
  audio: true
  system_timer: true
  network_hosts: ["api.open-meteo.com"]
```

**Absent means denied, not unrestricted.** No `network_hosts` key means
`http.fetch` is not reachable from that app at all. Permissions are enforced by
the host at the ABI call site, never by the interpreter, and are shown to the
user at install time.

`bar_style` resolves `DESIGN.md` §12's open question #4 for script apps the same
static way it does for bundled ones: per-app, declared, not dynamic.

## 4. `.capp` package format

Uncompressed TLV stream. Uncompressed because LVGL binary images should be read
from flash in place rather than inflated into RAM, which is the same reason the
Calculator port moves its icon out of a 906KB C array.

```
magic "CAPP" | u16 fmt_ver | u32 manifest_len | manifest.json
entries[] : u16 path_len | path | u32 len | u32 crc32 | bytes
trailer   : u8 sig_alg | ed25519 signature over all bytes above | u8 key_id
```

Streamable front-to-back, which a ZIP central directory is not — that matters
when the install transport is a serial byte stream and RAM is the constraint.

`crystal pack ./clock3p` converts the YAML, hashes each entry, signs, and emits
`<id>-<version>.capp`.

Install is transactional: stream to `/apps/.staging/`, verify every CRC and the
signature, then rename into place and write the registry row. A power cut leaves
a staging directory and no half-installed app; boot prunes it.

Signature required by default. Unsigned packages install only in Developer Mode,
behind an explicit confirm.

## 5. Registry and boot reconcile

`CrystalAppEntry` generalises into a record carrying its origin:

```c
typedef enum { CRYSTAL_ORIGIN_BUILTIN, CRYSTAL_ORIGIN_PACKAGE } crystal_app_origin_t;
```

Boot sequence:

1. Walk the compiled-in table → builtin records.
2. Scan `/apps/*/app.json` → package records. A malformed manifest is skipped
   with a log line, not a boot failure.
3. Join both against NVS rows by id.
   - Folder with no row → newly sideloaded. Assign the next free slot, enabled.
   - Row with no folder or builtin → deleted behind the OS's back. Prune the row.
4. Sort by slot, `installApp()` the enabled ones in order.

Step 3's second case is what makes hand-deleting a folder over USB a legitimate
uninstall. It is also why arrangement must not live in a file next to the apps:
the reconcile needs one authority, and NVS is the one that is power-fail safe and
built for small mutable values. A drag-reorder writes two u16s to NVS instead of
rewriting a file that also contains developer-authored data.

The launcher and Manage Apps see one merged, ordered list and cannot tell the
tiers apart — except for the one difference the UI must show: **builtin apps
disable, package apps uninstall.** Uninstall deletes the folder, so it also
clears state; disable does not.

Icons for package apps load from `assets/` through an LVGL filesystem driver,
the same mechanism v1 already needs for moving bundled icons out of the binary.

## 6. Host ABI v1

Handles, not pointers: every object is a `u32` index into a per-app table, so a
fabricated or stale handle is a caught error rather than a jump into garbage.
All handles die at `onDestroy`.

The widget catalog is fixed. It is sized by the three bundled apps plus the
worked example — if Clock, Weather, and Calculator can be written with it, it is
enough to open.

### 6.1 `crystal.ui` — tree

| Function | Notes |
| --- | --- |
| `screen()` | root container for this app's visual area |
| `container(parent, opts)` | flex or grid |
| `row(parent, opts)` / `col(parent, opts)` | `gap`, `wrap`, `flex` |
| `label(parent, opts)` | `text`, `font`, `align`, `opacity` |
| `button(parent, opts)` | `on_click`; `min_height` defaults to 44 |
| `image(parent, opts)` | `src` is a bundle-relative path |
| `list(parent, opts)` | `list_add`, `list_insert`, `list_clear` |
| `slider` / `switch` / `arc` / `chart` / `textarea` | `on_change` where applicable |
| `tabview(parent, opts)` / `tab(tabs, n)` | |
| `delete(h)` | |

### 6.2 `crystal.ui` — properties, layout, timers

| Function | Notes |
| --- | --- |
| `set_text(h, s)` / `get_text(h)` | |
| `set_value(h, n)` / `get_value(h)` | slider, arc, switch, chart |
| `set_style(h, tbl)` | whitelisted keys only: colour, radius, padding, opacity, font, border, align, size, flex |
| `set_hidden(h, bool)` / `set_enabled(h, bool)` | |
| `scroll_to(h, opts)` | honours the §7 keyboard rule automatically |
| `on(h, event, fn)` | `click`, `long_press`, `change`, `focus`, `scroll` |
| `timer(period_ms, fn)` / `timer_delete(h)` | LVGL timers; never a busy loop |
| `after(delay_ms, fn)` | one-shot |

Fonts are the three sizes from `DESIGN.md` §10 — `small` 16, `medium` 20,
`large` 28 — named, not numeric, so trimming the compiled set never breaks an app.

### 6.3 `crystal.service`

| Function | Notes |
| --- | --- |
| `time.now()` | epoch seconds |
| `time.format(fmt)` | strftime, in the OS timezone |
| `time.timezone_name()` | |
| `timer.arm(sec)` / `start()` / `pause()` / `resume()` / `reset()` | the OS-owned countdown |
| `timer.status()` | `{state, remaining, total}`; state is `idle`/`ready`/`running`/`paused`/`finished` |
| `notify.toast(text)` | non-interactive, 2500ms hold |
| `notify.chime()` | requires `audio` |
| `http.fetch(url, opts, cb)` | async; host-allowlisted; returns a request handle |
| `http.cancel(req)` | |
| `net.status()` | `{state, ssid, ip}` — read-only, no credentials |
| `location.get()` | lat/long from Settings › General, if permitted |
| `system.info()` | OS version, ABI version, free PSRAM |

`timer.*` is the important entry. It hands an absolute end instant to
`crystal_service`, which owns expiry, the toast, and the chime independently of
any app. A script app therefore gets the same "timer survives destruction"
guarantee the bundled Clock has, without being trusted to keep it.

`http.fetch` is async by rule. No blocking call is reachable from a script,
because script runs on the LVGL task and a blocking call there stalls the panel
and eventually trips the 5s task watchdog.

### 6.4 `crystal.state`

| Function | Notes |
| --- | --- |
| `get(k)` / `set(k, v)` / `erase(k)` | nil value erases |
| `flush()` | forces a write; implicit at `onPause` |

Numbers, strings, booleans, and flat tables. Backed by `data/state.json` for
script apps and by NVS `CrystalState` for builtins. Raw NVS is not in the ABI at
all — that boundary is what keeps WiFi credentials out of app reach, as
`IMPLEMENTATION_PLAN.md` §Phase 4 already noted.

### 6.5 `crystal.fs`

`read(path)`, `exists(path)`, `list(dir)` over the bundle (read-only) and
`data/` (read-write, quota'd). Paths are bundle-relative; `..` is rejected. No
absolute paths, no access to another app's folder, no access to `/spiffs`.

### 6.6 Lifecycle

The app module returns a table of hooks mapping one-to-one onto `CrystalApp`:
`onCreate`, `onStart`, `onPause`, `onResume`, `onStop`, `onDestroy`, `onBack`.
All are optional. There are no install/uninstall callbacks; installation and
clear-data are platform operations. Once-per-boot setup, when unavoidable, runs
from `onCreate` behind module-local or instance-local guard state.

The contract authors must be told, because guessing wrong is silent data loss:

- `onCreate` runs on **every** launch, on a fresh tree. `max_running_num = 1`
  means there is no restored path to branch on (`IMPLEMENTATION_PLAN.md` §4.5).
- `onPause` is the only guaranteed write point, and precedes `onDestroy` on every
  path including return-to-launcher.
- Handles do not survive `onDestroy`. Rebuild in `onCreate`.
- `onStop` fires when an OS overlay fully covers the app. Stop timers there — the
  panel measured 7-8 FPS at gate G1 and redrawing behind an opaque overlay costs
  an animation its frames.
- Return values are advisory. A false return is logged, not fatal — same fix as
  §4.5 item 5.
- `onCreate` has an 80ms budget, warned in debug builds.

## 7. Runtime tiers

| Tier | Package | Author needs | Est. cost | For |
| --- | --- | --- | --- | --- |
| Declarative | `ui.json` + bindings | text editor | ~20KB flash | dashboards, readouts, remotes |
| Script | `main.lua` | text editor | ~150-200KB flash, ~40KB RAM/app | the real third-party tier |
| Native | prebuilt PIC ELF | xtensa toolchain | ~15KB loader | curated / first-party only |

Costs are estimates from typical embedded builds, **not measured on this board.**
A flash/RAM spike gates Phase 17.

**Lua 5.4 for the script tier.** Chosen less for the language than for two hooks:
a documented custom allocator (the per-app memory ceiling) and an
instruction-count hook (the responsiveness watchdog). Both are load-bearing in
§8, and an interpreter without them cannot be contained on a single-threaded UI.
QuickJS has wider developer reach at roughly 2-3x the flash and a heavier GC,
which is the wrong trade on a bandwidth-bound panel. WASM via WAMR is the
upgrade path if real isolation or multi-language becomes worth its cost, and it
slots in behind this same ABI unchanged — which is the point of §1's first bullet.

Build the **declarative** tier first even though it is the least powerful. It
forces the widget façade to be designed and frozen before any third party depends
on it, and it ships the panel and package pipeline end-to-end with no interpreter
risk at all.

**Native** is possible on the S3 today: `SPIRAM_FETCH_INSTRUCTIONS` is already
enabled, which is the enabling condition for loading position-independent ELF into
PSRAM and calling it. It needs a spike. Scope it to curated apps, because it means
freezing an exported symbol table as a permanent ABI and accepting that a bad app
corrupts the OS heap instead of raising an error.

## 8. Sandbox and safety

**Responsiveness watchdog.** Script must run on the LVGL task, so a runaway loop
holds the LVGL lock and trips `CONFIG_ESP_TASK_WDT_TIMEOUT_S=5` — the failure
mode `IMPLEMENTATION_PLAN.md` §Phase 12 already flags. A Lua count hook every
~10k VM instructions checks a per-callback deadline of 50ms. Overrun raises an
error, the app is torn down through the normal lifecycle, and the user gets a
toast: *"Clock stopped responding."* The OS stays up.

**Memory ceiling.** A per-app allocator draws from PSRAM against `memory_kb`.
Exhaustion is a clean Lua error, then teardown — never a system panic.

**Environment.** Own `_ENV` per app. `os`, `io`, `package`, `require` of anything
outside `crystal.*`, `load`, and `loadstring` are all removed. `os.time` is
re-provided through `crystal.service`.

**Filesystem.** `/apps/<id>/` read-only, `/apps/<id>/data/` read-write and
quota'd. Nothing else is addressable.

**Crash containment.** A failing app is disabled after three consecutive launch
failures, with a notice in Manage Apps rather than a boot loop.

What this does **not** protect against: native-tier apps (no MMU), or an app
filling its quota with junk. Both are accepted.

## 9. Control panel

Two panels, one API, so they can never disagree about state.

- **On-device Manage Apps** (`DESIGN.md` §9, Phase 13). Reorder, install/uninstall,
  clear data. What ordinary users touch. No cable, no computer.
- **External panel** — everything the on-device panel does, plus the operations
  that need a file: install a `.capp`, stream logs, sideload during development.

```
GET    /api/v1/apps                 merged list with origin, version, state
POST   /api/v1/apps                 stream a .capp, verify, install
DELETE /api/v1/apps/{id}            uninstall (package) / disable (builtin)
PATCH  /api/v1/apps/order           [{id, slot}, ...]
POST   /api/v1/apps/{id}/enabled    {enabled: bool}
DELETE /api/v1/apps/{id}/data       clear state
POST   /api/v1/apps/{id}/launch     developer mode
GET    /api/v1/system               versions, ABI version, free flash/PSRAM
GET    /api/v1/logs                 SSE stream
```

Two carriers, identical JSON:

- **WiFi** — `esp_http_server`, static web UI from SPIFFS, mDNS at
  `crystal-<serial>.local`.
- **USB** — native USB-OTG as TinyUSB CDC-ACM, same messages framed on a byte
  stream. With Web Serial the same browser page drives USB installs: no driver, no
  desktop app, one panel for both. USB MSC ("drop a file on a drive") is rejected
  despite the appeal — the host owns the block device while mounted, which makes
  transactional install and concurrent OS reads unworkable.

**Security, stated plainly rather than implied.** This API installs code and reads
device state. It is **off by default** until enabled in Settings › Developer. On
enable the device shows a 6-digit pairing PIN *on its own screen* — there is a
display, so use it instead of asking for a password on a 480px keyboard — which is
exchanged once for a bearer token. LAN-only; never exposed to the internet, and
not hardened for it.

**Developer CLI**

```
crystal pack ./clock3p              # -> com.example.clock3p-1.0.0.capp
crystal install --usb ./clock3p     # push the folder; unsigned, dev mode only
crystal logs -f                     # stream Lua errors
crystal run com.example.clock3p     # install + launch
```

`logs -f` and `run` are not conveniences. Without them the edit-test loop is
unusable and nobody will write a second app.

## 10. Partition change — do this before shipping

None of the above fits the current table, and `IMPLEMENTATION_PLAN.md` §5 already
states the cost of getting this wrong: repartitioning after devices ship means a
serial reflash of every unit. Core is expected at 2.5-3MB, so 5M slots have room
to give.

```
# Name,     Type, SubType,   Offset, Size
nvs,        data, nvs,       ,       0x6000
nvs_keys,   data, nvs_keys,  ,       0x1000
otadata,    data, ota,       ,       0x2000
phy_init,   data, phy,       ,       0x1000
coredump,   data, coredump,  ,       0x10000
ota_0,      app,  ota_0,     ,       4M       # was 5M
ota_1,      app,  ota_1,     ,       4M       # was 5M
storage,    data, spiffs,    ,       2M       # OS assets
apps,       data, littlefs,  ,       5.5M     # app folders + app data
```

**LittleFS, not SPIFFS, for `apps`.** Real directories, power-fail safe, and far
better wear behaviour under repeated install/delete. This partition is rewritten
constantly; SPIFFS has none of those properties. `storage` stays SPIFFS since OS
assets are written once at flash time.

## 11. Phases

Numbered after the existing plan. 14 and 15 are independently useful and carry no
interpreter risk.

### Phase 14 — Package format and registry generalisation

`.capp` spec and `crystal pack`; LittleFS `apps` partition; `origin` on the
registry record; boot reconcile per §5; icons loaded from bundle files.

Exit: a bundled app converted to a package installs, launches, reorders, and
uninstalls cleanly. Hand-deleting its folder over USB leaves no orphan NVS row.

### Phase 15 — Control panel

HTTP API, static web UI, CDC-ACM carrier, Web Serial page, pairing PIN, signature
verification, `crystal` CLI with `logs -f`.

Exit: a `.capp` installs over both WiFi and USB from the same browser page. The
API refuses every request without a paired token. On-device Manage Apps and the
external panel agree after a change made from either.

### Phase 16 — Declarative runtime

`DeclarativeApp : CrystalApp`, the §6 widget façade, bindings, expression
evaluator. **ABI v1 is frozen at the end of this phase** — it is the last moment
changing it is free.

Exit: a declarative app renders, binds live service values, and survives
switch-away/switch-back. The façade builds all three bundled apps' UIs without an
addition.

### Phase 17 — Lua runtime

`ScriptApp : CrystalApp`, sandbox, count-hook watchdog, per-app allocator, `data/`
quota, `crystal run`. Gated on the flash/RAM spike from §7.

Exit: `examples/clock3p/` runs as a real app. A deliberate infinite loop in a
callback produces a toast and a dead app, not a watchdog reset. A timer armed by
the script still fires after the app is destroyed.

## 12. Open questions

1. **Curated catalog transport.** A static signed JSON index plus packages on a
   CDN needs no build infrastructure and is nearly free. The server-side build
   service is the alternative and is a business commitment — build infra,
   versioning, a full ~5MB reflash per change. Decide before Phase 15, since the
   panel's browse view depends on it.
2. **Are package apps in the app-switch ring?** A script app killed by the
   watchdog mid-drag is a case Phase 6's 50% crossover does not define.
3. **ABI stability promise.** How long does `abi: 1` live, and are two majors
   supported at once? Answer before Phase 16 ships, not after.
4. **Declarative and script in one package.** Allowing `ui.json` for layout plus
   `main.lua` for behaviour is attractive and doubles the loader's surface. Not v2.
