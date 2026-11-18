# Crystal OS Lua app and CAPP developer reference

**Application ABI:** 1 · **Reviewed:** 2026-11-19 · **OS version reviewed:** 0.17.0

Use this reference to write a Lua application for Crystal OS, package it as a
`.capp`, install it on a device, and distribute it to users. You supply Lua
source, an application manifest and optional assets. The device supplies the
`crystal.*` APIs for UI, storage, time, networking and notifications.

A `.capp` is the installable form of your app: it contains the manifest, Lua
source and assets, plus integrity information and a signature trailer. You do
not need to compile or modify Crystal OS to create a Lua app. You need Python
and the supplied packaging tool on your computer, and a compatible Crystal OS
device for testing.

Start with [your first app](#your-first-lua-app), then follow
[packaging and installation](#package-install-and-distribute). The rest of this
reference defines the app-facing contracts and limits. The
[CAPP format appendix](#appendix-capp-binary-format) is for developers writing
packaging tools; the supplied packer handles those bytes for you.

## Contents

- [Your first Lua app](#your-first-lua-app)
- [Package, install, and distribute](#package-install-and-distribute)
- [Package layout and manifest](#package-layout-and-manifest)
- [Compatibility and versioning](#compatibility-and-versioning)
- [Lifecycle and execution](#lifecycle-and-execution)
- [Lua UI reference](#lua-ui-reference)
- [Lua services](#lua-services)
- [Persistence and JSON](#persistence-and-json)
- [Test and troubleshoot your app](#test-and-troubleshoot-your-app)
- [Appendix: CAPP binary format](#appendix-capp-binary-format)

## Your first Lua app

Create these two files in a new `hello/` directory. The manifest is JSON in
`app.yaml`, which the packer accepts without requiring YAML syntax support.
The chosen quotas are example inputs, not universal minimum requirements.

`app.yaml`:

```json
{
  "abi": 1,
  "id": "com.example.hello",
  "name": "Hello Crystal",
  "version": "1.0.0",
  "min_os": "0.17.0",
  "runtime": "lua",
  "entry": "main.lua",
  "bar_style": "dark",
  "memory_kb": 160,
  "storage_kb": 8,
  "worker_threads": 0,
  "permissions": {}
}
```

`main.lua`:

```lua
local ui = crystal.ui
local state = crystal.state
local label
local count = 0

return {
    onCreate = function()
        local saved = state.get("count")
        if math.type(saved) == "integer" and saved >= 0 and saved < 1000000 then
            count = saved
        end
        local column = ui.col(ui.screen(), {gap = 12})
        if not column then return false end
        label = ui.label(column, {text = "Count: " .. count})
        if not label then return false end
        local button = ui.button(column, {text = "Increment", height = 48})
        if not button then return false end
        return ui.on(button, "click", function()
            local next_count = (count + 1) % 1000000
            if state.set("count", next_count) then
                count = next_count
                ui.set_text(label, "Count: " .. count)
            else
                print("Could not save count")
            end
        end)
    end,
    onDestroy = function()
        label = nil -- Host teardown deletes the owned UI and callbacks.
    end,
}
```

This app saves each user action, checks storage/creation failures and registers
one callback for its lifetime. It needs no network, notification or timer
permission. For fuller applications see [Clock](examples/clock3p/),
[Bus Lite](examples/bus3p/).

## Package, install, and distribute

Run these commands from the Crystal OS checkout containing `tools/crystal.py`.
Create your `hello/` app folder there, or replace `hello` with its actual path.
The app's `assets/` directory is optional. Keep package outputs outside the app
folder so an earlier `.capp` does not get included in a later package.

### Prepare the packaging tool

```sh
python3 -m venv /tmp/crystal-app-venv
. /tmp/crystal-app-venv/bin/activate
python3 -m pip install -r tools/requirements-pack.txt
```

The example's JSON manifest can be packed with Python's standard library.
The listed dependencies add YAML input and Ed25519 signing/verification.

### Pack and inspect

```sh
python3 tools/crystal.py pack hello --unsigned -o /tmp/hello.capp
python3 tools/crystal.py inspect /tmp/hello.capp
```

Inspect checks the container, manifest, file list and CRCs. It does not run your
Lua app or prove that it fits the device's available memory. The example
produces a package with ID `com.example.hello` and entry `main.lua`.

### Install a development package over USB

USB is the physical developer-mode installation path and accepts unsigned
packages. Close serial monitors before installing. Replace the port below with
your device's serial port; this CLI transport requires a POSIX host.

```sh
python3 tools/crystal.py install /tmp/hello.capp --usb --port /dev/cu.usbmodem101
```

Check the command's result. If it reports `restart_required: true`, restart the
device, then open **Hello Crystal** from the launcher. Tap Increment, close and
reopen the app, and confirm the saved count remains. Packaging and installing
are separate from launching; a successful install is not a successful app test.

### Sign for distribution or Wi-Fi installation

Use your own Ed25519 package-signing private key. The matching public key and
key ID must already be trusted by the target device; ask the device/platform
maintainer which signing identity is accepted. Signing alone does not establish
that trust. Keep your private key out of the app folder and distributed files.

```sh
python3 tools/crystal.py pack hello --key /path/to/package-private.pem --key-id 1 -o /tmp/hello.capp
python3 tools/crystal.py verify /tmp/hello.capp --trusted-key 1=/path/to/package-public.pem
```

The paths are placeholders for your Ed25519 PEM keys. `verify` authenticates
the package using the supplied public key; plain `inspect` only describes its
signature. Distribute the resulting `.capp`, its version, required OS version,
permissions, and the signing identity accepted by your intended devices.

For Wi-Fi installation, enable the device's Local Control Panel in Developer
Tools and use its displayed address and pairing PIN. Substitute those values
for the illustrative address and PIN below:

```sh
python3 tools/crystal.py install /tmp/hello.capp --wifi http://192.168.1.20/ --pin 123456
```

The result includes a session token. Follow the returned restart requirement;
a restart requires a fresh panel session and pairing. The panel can also be
used to install/manage packages. See the [host tools guide](tools/README.md)
for transport details.

### Launch and inspect logs during development

For a **not-yet-installed app ID**, the signed `run` command packs, installs,
requests launch and follows logs:

```sh
python3 tools/crystal.py run hello --wifi http://192.168.1.20/ --pin 123456 --key /path/to/package-private.pem --key-id 1
```

For an existing session, follow logs using the token returned by `install`:

```sh
python3 tools/crystal.py logs -f --wifi http://192.168.1.20/ --token YOUR_SESSION_TOKEN
```

Use `print(...)` for app diagnostics. A serial monitor can also show app/runtime
logs after USB installation; reopen it only after the install command finishes.

### Reinstalling and publishing updates

**The current installer rejects an already-installed app ID**, even when the
new package has a higher `version`. `run` also uses that installer; it is not
hot reload. Uninstall the previous development package before trying another
build with the same ID. Uninstall deletes its saved data, so use disposable test
data while iterating. A data-preserving update mechanism is not available in
this version.

A package can be disabled after three consecutive failed launches. Fix the
launch error before re-enabling it. If uninstall reports active resources,
restart the device and retry; if installation reports cleanup in progress,
wait for cleanup and retry.

## Package layout and manifest

Authoring layout:

```text
hello/
  app.yaml                 YAML or JSON manifest, consumed by the packer
  main.lua                 Lua source entry
  assets/                  Optional immutable package content
  README.md                Developer documentation; omitted by the packer
```

The installer writes the embedded manifest as `app.json`, copies payload
files, and creates an initially empty `data/` directory. Never ship `data/`: the
packer excludes it and the installer rejects payload entries under it.
`app.yaml` and `app.json` are omitted from payload entries by the packer.
Other files under the authoring directory are packaged, including nested
documentation, unless excluded by the packer's explicit rules.

### Manifest fields

All sizes ending in `_kb` mean multiples of **1024 bytes**. Text limits below
are byte limits in the firmware, not displayed character counts.

| Field | Required | Contract |
| --- | --- | --- |
| `abi` | Yes | Integer `1` |
| `id` | Yes | At most 63 ASCII bytes; at least two dot-separated segments, each starting with `a`–`z`, followed by lowercase letters, digits or `-`; e.g. `com.example.hello` |
| `name` | Yes | Nonempty display name, at most 31 bytes |
| `version` | Yes | App version, three numeric components |
| `min_os` | Yes | Minimum OS version, three numeric components |
| `runtime` | Yes | Set to `lua` for applications in this reference |
| `entry` | Yes | Existing package-relative file; cannot be `app.json`, `data`, or under `data/` in a packer-produced package |
| `icon` | No | Existing package-relative file below `assets/`; omit if unused |
| `bar_style` | Yes | `dark` or `light`; accepted metadata; currently does not change the Lua app bar appearance |
| `memory_kb` | Yes | Integer 1–8192; admitted memory reservation; Lua uses this to size its VM arena |
| `storage_kb` | Yes | Integer 0–5632; writable data reservation |
| `worker_threads` | Yes | Integer 0–2; for Lua, the active HTTP request quota, not a number of dedicated tasks |
| `permissions` | No | Object described below; omitted booleans default to false |

Descriptive keys such as `author`, `license` and `description` may be included
but do not grant capabilities. Duplicate manifest keys are rejected. The
embedded JSON manifest is at most 8192 bytes and must fit the firmware parser's
depth/complexity checks. Avoid arbitrary nested metadata.

Package paths use ASCII letters, digits, `.`, `_`, `-` and `/`; maximum 160
bytes overall and 63 bytes per component. Absolute paths, backslashes, empty
components, `.`/`..` components and trailing slashes are invalid. Package
payload names cannot start with `.`; symlinks are rejected by the packer.

### Permissions and admission

| Permission | Enables | Denial/result behavior |
| --- | --- | --- |
| `notify: true` | `service.notify.toast` | Otherwise silently does nothing |
| `audio: true` | `service.notify.chime` | Otherwise returns false; success is not proof of audible hardware output |
| `system_timer: true` | Mutating OS countdown operations | Otherwise mutation fails; reading timer status needs no permission |
| `location: true` | Reading the OS location | Returns nil if denied or unavailable |
| `network_hosts: [...]` | Exact-host HTTP requests | Unlisted hosts reject; requires `worker_threads > 0` |

At most 16 lowercase hostnames are allowed, each at most 253 bytes with labels
of 1–63 bytes. No wildcards, schemes, paths or ports belong in the allowlist.
The URL may specify a valid port; host permission is not restricted to one port.

The device may reject installation when installed apps' combined memory,
worker or storage reservations exceed its available budget. Disabled packages
still count toward reservations, and there are at most 32 installed packages.
Choose quotas for your app's actual needs; the allowed field ranges do not
promise that every device can admit their maximum values.

Lua's arena does not account for all native allocations: LVGL widgets, images,
HTTP buffers and JSON parsing also consume host memory. A successful admission
does not guarantee every allocation or callback will succeed.

## Compatibility and versioning

Three independent versions appear in an application:

| Field | Meaning | Current check |
| --- | --- | --- |
| CAPP header version | Package container format | Exactly `1` |
| Manifest `abi` | Application interface major | Exactly integer `1` |
| Manifest `min_os` | Minimum firmware version | Numeric `major.minor.patch` comparison against the running OS |

`version` is the application's version, using the same three-component syntax.
Components are 0–65535; leading zeroes, prerelease labels and build suffixes are
not accepted. `min_os` is checked on the device; packing alone cannot prove
compatibility with a particular device.

`crystal.service.system.info()` returns `os_version` and `abi_version`. There is
no separate minor ABI negotiation or capability registry. Additions in this
tree, including readiness subscriptions and `ui.list_set_text`, still report
ABI 1 and project version 0.17.0. **The version string alone cannot distinguish
all development builds.** Check for an optional function before using it, or
document the firmware revision against which the app was tested:

```lua
local time_service = crystal.service.time
if type(time_service.on_ready) == "function" then
    time_service.on_ready(function(event)
        if event == "ready" then print("Clock is ready") end
    end)
end
```

## Lifecycle and execution

A Lua entry is **text source**, at most 64 KiB. Binary Lua chunks are rejected.
The module must return a table; it is not called with an app object or context:

```lua
return {
    onCreate = function() return true end,
    onPause = function() return true end,
    onResume = function() return true end,
    onDestroy = function() return true end,
    onBack = function() return false end,
}
```

Hooks are optional in the current dispatcher. A present hook must be a
function. Define `onCreate` for UI setup and explicit cleanup hooks when your
app needs them. Hooks receive no arguments.

| Transition | Call and behavior |
| --- | --- |
| Fresh launch | Load/evaluate entry, then `onCreate()`; there is **no initial `onResume()`** |
| Pause | `onPause()`, cancel outstanding app HTTP requests, pause UI timers, cancel animations and dismiss the app keyboard |
| Resume of retained app | Resume UI timers, then `onResume()`; deferred time/network notifications follow a successful hook |
| Close | Pause first if needed, then `onDestroy()`; discard VM, root/widgets, callbacks, timers, images and pending requests |
| Failed create | Invoke teardown; a failed VM is not re-entered for Lua cleanup |
| Back | Only literal boolean `true` from `onBack()` consumes Back; otherwise use the shell/default close path |

For create/resume, literal boolean `false` rejects the operation; nil/no return
and other values are accepted. Pause/destroy return values do not veto host
cleanup. No `onStart`, `onStop`, `onInstall` or `onUninstall` Lua hooks are
called. Treat UI handles
and VM globals as disposable: a later fresh launch starts a new VM.

All Lua hooks, UI callbacks and delivered HTTP callbacks run on the UI task.
Keep work bounded and return promptly. Coroutines are cooperative Lua execution,
not worker tasks or a background service. Use asynchronous HTTP rather than
waiting in a loop; UI timers run only while the app is active.

### Sandbox, numbers and failures

The bundled Lua 5.4 configuration uses **32-bit signed integers and 32-bit
floats**. Keep Unix timestamps as integers; converting to floating point loses
second-level precision at current epoch values. Do not assume desktop Lua's
64-bit integer/double behavior. UI handles are opaque positive integers; do
not construct, serialize or perform arithmetic on them.

The runtime removes `os`, `io`, `package`, `require`, `dofile`, `loadfile`,
`load`, `loadstring`, `collectgarbage`, and `debug`. The remaining standard
libraries include table, string, math, UTF-8 and coroutine facilities. There is
no supported external module or C extension loading. Bundle logic into the
entry source at build time; read data/assets through `crystal.fs`. `print`
routes to the firmware log with a bounded line buffer.

The entry evaluation and each protected callback use a **5-second deadline**,
or **30 seconds** when the developer extended-timeout setting is enabled.
Instruction hooks check every 10,000 Lua instructions, including wrapped
coroutines. This is a cooperative limit, not preemption of a native C call or
a performance target. A detected timeout stays fatal even if caught with
`pcall`. An uncaught Lua error or final protected-call OOM marks the VM failed,
reports an app-stopped toast and schedules closure where applicable. Later
hooks are skipped and native teardown still runs.

Startup may log a warning if `onCreate` takes over 200 ms or `onResume`
takes over 80 ms. These are responsiveness diagnostics, separate from the
execution deadline. Keep callbacks short even when the deadline is longer.

## Lua UI reference

Use `local ui = crystal.ui`. Function calls use dot syntax. Successful creators
return a handle; failure returns nil. Most mutations return a boolean, and
getters return a value or nil. Do not assume every failure is `nil, error`: HTTP
has that convention, UI operations generally do not.

Handles belong to one app and one lifetime. Deleted, stale, fabricated,
cross-app and wrong-type handles fail. Deleting a container invalidates its
descendants and chart series. Root deletion/reparenting and parent cycles
reject. UI positions/sizes are pixels; `get_size` reports screen coordinates.

### Creation

`ui.screen()` returns the app root. These creators take `(parent, options?)`:

```text
container  row  col  scroll_view  label  button  image  list
slider  switch  arc  chart  textarea  tabview  spinner
```

`ui.dialog(options)` is the exception: it uses the app root without a parent
argument. There is no exported `ui.create`, `ui.button_matrix` or `ui.tab_add`.

| Lua creation option | Implemented behavior |
| --- | --- |
| `width`, `height` | Integer 1–960 pixels when supplied |
| `min_height` | Integer 1–480 |
| `gap` | Integer 0–128; sets row and column padding |
| `text` | Initial label/button/textarea text |
| `font` | `small` = 16 px, `large` = 28 px, otherwise 20 px |
| `style` | Style table below |
| `min`, `max`, `value` | Integer control range; defaults 0, 100, 0; `min < max`; switch uses integer 0/1 |
| `placeholder` | Textarea placeholder |
| `src` | Image path below `assets/` |
| `tab_labels` | Tabview array of 1–8 titles; use `ui.tab(view, zero_based_index)` to get a tab |
| `title`, `message` | Required strings for dialog; current Lua dialog supplies a fixed OK button and close control |
| `on_click`, `on_change`, `on_focus`, `on_blur` | Optional callbacks receiving the widget handle |
| `on_select` | List callback receiving a **one-based row index**, not a widget handle |

Container/column/scroll-view creation defaults to column flex layout; `row`
uses row flex layout. Containers default to full parent width and content
height. Create children with subsequent creator calls; Lua does not recursively
build generic `children` trees (tab definitions are a special case).

The Lua creation API does not support options such as `x`, `y`,
`flex_wrap`, `max_length`, `one_line`, `item_height`, initial list `items`, or
chart `point_count`/`chart_type`/initial `series`. Unknown creation options can
be ignored, so successful construction is not feature detection. Use explicit
setters where provided. A Lua chart retains the bundled LVGL default capacity
of **10 points**; the API's 128-point ceiling does not increase that capacity.

### Properties and events

| Function | Return / details |
| --- | --- |
| `ui.tab(view, index)` | Tab handle/nil; zero-based index 0–7 |
| `ui.delete(handle)` | Boolean; deletes owned widget subtree |
| `ui.set_text(handle, text)` | Boolean; at most 4096 bytes |
| `ui.get_text(handle)` | String/nil; labels, button labels and textareas |
| `ui.set_value(handle, value)` | Boolean; integer in slider/arc range, 0/1 for switch |
| `ui.get_value(handle)` | Integer/nil |
| `ui.set_hidden(handle, boolean)` | Boolean |
| `ui.set_enabled(handle, boolean)` | Boolean |
| `ui.set_style(handle, table)` | Boolean; see keys below |
| `ui.focus(handle)` / `ui.set_focus(handle)` | Boolean; textarea focus opens the system keyboard |
| `ui.scroll_to(handle, {animate=false, align="center"})` | Boolean; align is `top`, `center`, or `bottom` |
| `ui.set_parent(handle, parent)` | Boolean; parent must be a layout container |
| `ui.get_size(handle)` | `{w, h, x, y}` / nil |
| `ui.on(handle, event, callback)` | Boolean; replaces the listener for that event |
| `ui.off(handle, event)` | Boolean; detaches that event listener |

Events are `click`, `long_press`, `change`, `focus`, `blur`, `scroll`, `delete`.
The callback receives one handle; read text/value separately. A click on a
child without a click handler forwards to the nearest registered ancestor.
Programmatic setters do not synthesize user change events. A delete event's
handle is already invalid for further widget operations.

Lua style keys:

| Keys | Values |
| --- | --- |
| `bg_color`, `text_color`, `border_color` | Integer `0x000000`–`0xFFFFFF` |
| `opacity`, `bg_opa` | Integer 0–255 |
| `radius`, `padding`, `border_width` | Integer 0–255 |
| `font_size` | Integer 12–48, mapped to fonts: 12–16 → 16, 17–22 → 20, 23–28 → 28, 29–48 → 48 px |
| `align` | Use `left`, `center`, or `right` for text alignment |

Unknown style keys reject. Lua style application mutates keys as it visits
them; an invalid later key can leave earlier changes applied. Do not depend
on all-or-nothing style updates or on a particular Lua table iteration order.

### Lists and charts

| Function | Contract |
| --- | --- |
| `ui.list_add(list, item)` | Boolean; append |
| `ui.list_insert(list, index, item)` | Boolean; zero-based insertion index, including current length |
| `ui.list_set_text(list, index, text)` | Boolean; zero-based row, text ≤96 bytes; requires the row's first child to be a label, so icon rows reject |
| `ui.list_remove(list, index)` | Boolean; zero-based row |
| `ui.list_clear(list)` | Boolean |
| `ui.list_count(list)` | Integer/nil |
| `ui.chart_add_series(chart, color)` | Series handle/nil; RGB integer color |
| `ui.chart_set_data(chart, series, values)` | Boolean; nonempty one-based Lua array of integers; ≤128 and no more than chart capacity |
| `ui.chart_set_range(chart, min, max)` | Boolean; -32768 ≤ min < max ≤ 32767; disables auto Y range |
| `ui.chart_clear(chart)` | Boolean; clears data but retains series handles |

List items are a nonempty string or `{primary="...", secondary="...",
icon="assets/..."}`. Primary and secondary text are each at most 96 bytes.
Attach a list-wide `on_select` at creation to receive row selections; Lua's
item insertion does not bind per-item `on_click`. Lists allow 64 rows normally
or 256 with a list-wide selection callback. Those selection rows avoid one
UI handle per row; other adopted widgets still share the 192-object limit.

**Index exception:** list operations and tab lookup are zero-based;
`on_select(index)` is one-based. Convert with `index - 1` before passing a
selection index to `list_remove` or `list_set_text`. It is not an event accepted
by `ui.on`/`ui.off`.

Charts allow four series per chart, sixteen per app. Data values are
-32768…32766; 32767 is reserved for missing points. Data arrays use normal
one-based Lua indexing.

### UI timers, animations and callback capacity

| Function | Contract |
| --- | --- |
| `ui.timer(period_ms, callback)` | Repeating timer handle/nil; period 1–86400000 ms; callback receives timer handle |
| `ui.after(delay_ms, callback)` | One-shot handle/nil; same bounds; retired before callback runs |
| `ui.timer_delete(timer)` | Boolean |
| `ui.anim(widget, properties, duration_ms, options?)` | Animation handle/nil |
| `ui.anim_cancel(animation)` | Boolean; no completion callback on cancellation |

Animation properties are `opacity`, `radius`, `bg_color`, `text_color`.
Duration is 1–60000 ms. Options are `delay_ms` (0–60000), `easing`
(`linear`, `ease_in`, `ease_out`, `ease_in_out`; default `ease_out`) and
`on_done(animation_handle)`. Up to sixteen UI timers and sixteen animations
can be live. Timers pause/resume with the app; animations cancel on pause,
widget deletion and teardown. A timer is not a guaranteed wall-clock tick.

There are **64 retained Lua callback records per VM** for UI registrations,
timers, animation completion and readiness subscriptions. Currently, replacing
or removing UI listeners, deleting timers/widgets, and completing one-shots
does not reclaim their Lua callback record until teardown. Avoid repeatedly
registering handlers or scheduling new one-shots in an unbounded loop. Reuse
widgets and repeating timers. Time/network subscription replacement reuses
its record, and unsubscribe releases it. HTTP callbacks use separate tracking.
Creator success does not guarantee an inline callback was registered when
callback capacity is exhausted; budget registrations explicitly.

### Image format

Lua images and launcher icons load from immutable `assets/` paths, cached by
path. PNG/JPEG and network image URLs are not supported by this loader.
Accepted CR16 encodings are:

- Text: `CR16 <width> <height>` followed by exactly width × height
  whitespace-separated RGB hexadecimal values in 0…FFFFFF.
- Binary: ASCII `CR16`, little-endian unsigned 16-bit width and height, then
  tightly packed RGB565 pixels. This path requires the configured 16-bit,
  non-byte-swapped LVGL color format.

The Lua loader caps source bytes at 256 KiB, dimensions at 480 × 480, and
decoded pixels at 64 KiB **per image**. These constraints apply together; a
480 × 480 image exceeds the decoded limit. Cached images also consume device memory outside the Lua arena.
Reuse image paths and keep decoded image sizes small.

## Lua services

Use `local service = crystal.service`. There is no generic service lookup or
direct HAL/LVGL pointer access.

### Time and network readiness

| Function | Result / callback |
| --- | --- |
| `service.time.now()` | Current Unix seconds as Lua integer; may be untrusted before readiness |
| `service.time.format(format)` | Current local time as string/nil; format ≤64 bytes, output buffer 128 bytes; no timestamp argument |
| `service.time.timezone_name()` | Current `%Z` abbreviation, or `Local`; not an IANA zone ID |
| `service.time.is_ready()` | Boolean: OS clock is considered valid |
| `service.time.on_ready(callback)` | Boolean; one subscription per app; callback receives `"ready"` |
| `service.time.off_ready()` | Boolean indicating whether a subscription existed |
| `service.net.is_ready()` | Boolean: a network interface has an IP |
| `service.net.on_change(callback)` | Boolean; one subscription per app; callback receives `"connected"` or `"disconnected"` |
| `service.net.off_change()` | Boolean indicating whether a subscription existed |
| `service.net.status()` | `{state, ssid, ip}` |

Subscriptions are delivered through the core's LVGL dispatch, not inline at
registration. A time subscriber receives the current valid-clock revision
after registration and later revisions after clock updates; it is not a
one-shot promise. A network subscriber receives the initial state, then
network revision/readiness changes. Thus repeated `connected` events can
occur. Callback replacement is supported. There are sixteen core listener
slots for each service, shared across subscribers.

While paused, notifications coalesce into pending flags. Resume delivers
pending notifications after a successful `onResume`; network delivery uses
current readiness. Destroy removes subscriptions. Re-read current time or
network state in the callback rather than assuming a complete event history.

Network readiness is IP acquisition, not DNS, TLS, or remote-server health.
`net.status().state` can be `connected`, `connecting` or `disconnected`; its
`ssid` and `ip` fields currently come from the Wi-Fi adapter even when Ethernet
provides the active route. They are not a reliable active-interface report.

### OS countdown, notifications and location

| Function | Result / meaning |
| --- | --- |
| `service.timer.arm(seconds)` | Boolean; immediately starts 1–86400 seconds; requires `system_timer` |
| `service.timer.start()` | Boolean; restarts using the already configured duration; no argument |
| `service.timer.pause()` / `resume()` / `reset()` | Boolean; requires `system_timer` |
| `service.timer.status()` | `{state, remaining_sec, remaining, total_sec, total}`; aliases hold the same seconds |
| `service.notify.toast(message)` | No return values; ≤120 bytes; requires `notify` |
| `service.notify.chime()` | Boolean permission result; invokes the OS alarm path if allowed |
| `service.location.get()` | `{lat, long, city}` / nil; requires `location` and available OS location |

The countdown is a **single shared OS timer**, not one timer per app. States
are `idle`, `running`, `paused`, `finished`. It survives app closure/uninstall;
another app can change it. The location field is named `long`, not `lon`.
Audio availability depends on the board and OS settings.

### Asynchronous HTTP

```lua
local request_id, err = crystal.service.http.fetch(
    "https://api.example.com/status",
    {method = "GET", timeout_sec = 15},
    function(id, status, body)
        if status == 200 then print(body) end
    end
)
if not request_id then print(err) end
```

The example requires `api.example.com` in `permissions.network_hosts` and a
positive worker quota; the URL is illustrative, not a provided service.

`fetch(url, options, callback)` returns a request ID, or `nil, error_string`
before submission. Pass nil for default options while retaining callback as
the third argument. Completion receives **three arguments** `(id, status,
body)`, not a response table. `status` is the HTTP code or `-1` for a transport
error. Response headers and detailed transport errors are not exposed to Lua.

| Input/limit | Contract |
| --- | --- |
| URL | 1–255 bytes, no NUL, lowercase `http://` or `https://` scheme; exact case-normalized host match |
| Authority | No userinfo, backslashes, IPv6 literal or malformed port; explicit ports 1–65535 |
| `method` | GET or POST, normalized to uppercase; default GET |
| `timeout_sec` | Integer 1–60; default 15 |
| `headers` | Table of up to 8 name/value pairs; names 1–63 bytes, values ≤255; host HTTP validation also applies |
| `body` | Text ≤2048 bytes, no NUL; nonempty body prohibited on GET |
| Response | At most 64 KiB; must also fit available Lua arena when delivered |
| Attempts/redirects | One attempt; automatic redirects disabled |
| Dispatch | One queued completion per 50 ms dispatch-timer tick, subject to UI scheduling |

`service.http.cancel(id)` returns a boolean for an app-owned active request.
Successful cancellation suppresses its Lua callback. The active slot remains
occupied until completion is drained, so immediately submitting a replacement
can still report `worker quota exceeded`. Pause/destroy cancel all app-owned
requests and drop their callbacks. Restart needed work on resume. Queued
requests share the OS HTTP worker; the manifest does not create private tasks.

### System information

`service.system.info()` returns a table. Numeric sizes use the unit in the
field name; `_kb` values are integer KiB. Diagnostic values are snapshots,
not allocations promised to the caller.

| Group | Fields |
| --- | --- |
| Compatibility | `os_version`, `abi_version` |
| System heap/storage | `free_internal_kb`, `largest_internal_kb`, `free_psram_kb`, `largest_psram_kb`, `free_storage_kb` |
| Native sampling | `native_internal_low_kb`, `native_psram_low_kb`, `native_internal_delta_kb`, `native_psram_delta_kb` |
| Lua arena | `lua_arena_kb`, `lua_used_kb`, `lua_peak_kb`, `lua_largest_free_kb` |
| Resource counts | `app_tasks`, `app_timers`, `app_queues`, `app_allocations`, `app_objects`, `app_callbacks`, `app_http_callbacks` |
| App bytes | `app_tracked_bytes`, `app_storage_bytes` |
| JSON diagnostics | `json_peak_internal_bytes`, `json_peak_psram_bytes`, `json_decode_count`, `json_largest_input_bytes` |
| Shared HTTP diagnostics | `http_peak_body_bytes`, `http_min_tls_internal_bytes`, `http_min_tls_largest_bytes`, `http_min_tls_psram_bytes`, `http_completed`, `http_cancellations` |

Native deltas compare global free heap against the app-launch baseline; other
system activity affects them. Native low values are sampled when `info()` is
called. HTTP statistics are shared-service values, not per-app accounting.

## Persistence and JSON

### Lua state

| Function | Contract |
| --- | --- |
| `crystal.state.get(key)` | Decoded value/nil; missing, unreadable, invalid key and JSON null can all appear as nil |
| `crystal.state.set(key, value)` | Boolean; JSON-serializable value; nil deletes the key |
| `crystal.state.erase(key)` | Boolean; delete the key |
| `crystal.state.flush()` | Always true; currently a no-op because writes are synchronous |

Keys are nonempty, at most 63 bytes, with no NUL. Lua state is a JSON object
stored at `data/state.json`, at most 32 KiB total, within `storage_kb`. Each
write reads/modifies/rewrites that file. Save meaningful state changes rather
than writing every UI tick. Reserve this path for `crystal.state`.

The current generic Clear Data action does not reset Lua `data/state.json` or
other files in `data/`. If your app offers a reset command, erase its known
state keys with `crystal.state.erase`. Uninstall removes the package directory,
including Lua state and files. Do not tell users that reinstalling preserves data.

### Files

| Function | Contract |
| --- | --- |
| `crystal.fs.read(path)` | Binary-safe Lua string/nil; `assets/` or `data/`, up to 64 KiB |
| `crystal.fs.write(path, bytes)` | Boolean; only `data/`, up to 64 KiB; creates parent directories |
| `crystal.fs.exists(path)` | Boolean; regular files; asset check uses the 64 KiB read limit |
| `crystal.fs.list(directory)` | One-based array of at most 64 immediate names; unsorted; invalid/unreadable directory yields an empty table |

Use relative paths such as `assets/routes.json` and `data/settings.json`, or
`assets`/`data` for listing. The path restrictions from the package section
apply. There is no delete, append, seek or streaming API. Writes use a
temporary sibling file, sync and rename; physical free space is still required
in addition to the logical data quota.

### JSON conversion

`crystal.json.encode(value)` returns text/nil; `decode(text)` returns the
converted value/nil. Decode accepts at most 64 KiB, rejects embedded NUL and
trailing non-whitespace. Conversion preserves integral values within Lua's
integer range; other numbers become 32-bit floats.

Encoding accepts finite numbers, booleans, strings, nil, and tables. Strings
are limited to 4096 bytes without NUL. Object keys are nonempty and at most
63 bytes. Encoding is bounded to 512 visited values and depth 16. A nonempty
sequence becomes a JSON array; a table with zero sequence length becomes an
object. Mixed array/object keys reject. Function-valued `on_*` fields are
skipped for host callback options; do not use them as persistent data.

There is no JSON-null sentinel: null decodes to nil, losing object entries and
creating holes in arrays. Empty JSON arrays and objects both decode to empty
Lua tables; re-encoding an empty table produces `{}`. Decode does not enforce
the encoder's 512-node bound; values deeper than conversion depth 16 become
nil. Bound input complexity yourself and test memory use with real payloads.

## Test and troubleshoot your app

Test the actual `.capp` on the device and OS version you intend to support.
Desktop Lua may use different numeric precision and does not provide the
`crystal.*` APIs. A syntax check or successful package inspection cannot replace
running the app.

Before distributing a build, exercise these app behaviors:

- Fresh launch, navigation, Back, pause/resume and close/reopen. Start initial
  UI timers in `onCreate`; do not depend on an initial `onResume` call.
- Saved settings after reopening and restarting the device, plus your app's
  own reset flow. Handle failed state/file writes without losing the current UI.
- Every permitted network host, offline launch, reconnect, HTTP errors and
  large real responses. Restart needed HTTP work after resume.
- Repeated interaction and refreshes. Observe `service.system.info()` for
  Lua usage, callback count and storage use; reuse callbacks and widgets.
- Layout on supported display sizes. Query `ui.get_size(ui.screen())` rather
  than assuming the device resolution; keep controls inside the app area.

| Symptom | What to check |
| --- | --- |
| `App already installed` | Uninstall the previous test package; changing `version` alone does not replace it |
| Signature not trusted | Package key ID and public key must match the device's accepted signing identity |
| App requires a newer OS | Check `min_os` and the device version; optional APIs also need feature checks |
| Lua entry fails at launch | Entry must be text Lua, exist in the package and return a hook table; read runtime logs |
| UI creation/getter returns nil or mutation returns false | Validate arguments, handle lifetime and object/callback limits |
| UI timers never start | Create them in `onCreate`; they pause when the app is inactive |
| `worker quota exceeded` | Reduce concurrent requests; a cancelled request may retain its slot until completion is drained |
| `network host is not allowed` | Check the exact host in `permissions.network_hosts`, URL scheme and authority |
| App-stopped timeout or memory message | Shorten callback work, bound input size and reduce retained tables/images; inspect memory diagnostics |
| State write returns false | Check key/value validity, the 32 KiB state limit and the app's data quota |

Working Lua packages: [Clock](examples/clock3p/) for UI timers, persistence and
the shared countdown, and [Bus Lite](examples/bus3p/) for assets, lists,
HTTP and readiness subscriptions. Their manifests show the permissions and
budgets those apps request.

## Appendix: CAPP binary format

All multi-byte integers in the container are **unsigned, big-endian**, with no
padding, compression or implicit alignment.

| Order | Bytes | Field |
| --- | --- | --- |
| 1 | 4 | ASCII `CAPP` |
| 2 | 2 | Format version, `1` |
| 3 | 4 | Manifest JSON byte length, 1–8192 |
| 4 | Variable | Manifest JSON bytes |
| 5 | Repeated | File records described below |
| 6 | 2 | Zero path length: end marker |
| 7 | 1 | Signature algorithm: `0` unsigned, `1` Ed25519 |
| 8 | 64 | Signature bytes |
| 9 | 1 | Signing key ID |

Each file record is `u16 path_length`, path bytes, `u32 file_length`,
`u32 crc32`, then exactly `file_length` bytes. CRC is the conventional CRC-32
used by Python `zlib.crc32` (reflected polynomial `0xEDB88320`). There are at
most 128 file records; duplicate and reserved paths reject. Entry and icon
files must be present. The host packer caps manifest plus payload file bytes
at 5632 KiB, separately from runtime data reservations and container overhead.

Ed25519 signs the original bytes from `CAPP` through the two-byte end marker,
excluding the complete 66-byte trailer. It is pure Ed25519, not Ed25519ph. A
signed package needs a trusted key ID in 1–255 on the target device. An unsigned
package must have a zero algorithm, 64 zero signature bytes and zero key ID;
installation requires explicit Developer Mode. Extra bytes after the trailer
reject. CRC detects file corruption; it does not replace signature verification.
