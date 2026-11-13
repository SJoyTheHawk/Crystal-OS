# Phase 15/16 implementation record

Updated 2026-11-14. Implementation, host validation and the focused core device
check are complete for the scope below. This is a
development milestone: existing results count, and production readiness,
detailed profiling and hardening are deferred. Host results do not establish
ESP32 memory headroom or network reliability.

## Scope decisions

The original plan §9.2 and Phase 15 exit criteria explicitly included a
browser Web Serial installer. The user chose to remove that requirement:
normal installation uses the authenticated WiFi panel, and the existing USB
CLI remains a developer fallback when WiFi is unavailable. Both transports use
the same atomic package installer. USB is useful for recovery and development;
it does not need a second browser UI.

The catalog follows the plan's resolved §15.2 decision: the computer fetches a
signed HTTPS JSON index and signed prebuilt `.capp` files, verifies them, then
installs through the existing transport. There is no new on-device store or
server build service. Public hosting and production signing identities remain
deployment configuration; test key ID 240 is not a production identity.
Weather fetching and Calculator computation remain in their current runtimes.

## Phase 15

- Logs return one bounded SSE batch and a terminator, rather than sleeping in
  the serial HTTP server for 60 seconds. The browser and CLI follow using
  `?since=<last-id>` with a one-second reconnect interval. Reconnect does not
  replay already consumed lines. An expired cursor after reboot resets to
  current history. History retains at most 64 lines, until reboot; newly
  authenticated clients can read it. CR/LF in log messages cannot inject frames.
- Every batch rechecks the bearer token. `DELETE /api/v1/session` revokes it;
  browser sign-out immediately clears the local view and attempts revocation.
  If the browser is offline, remote revocation cannot happen until a request
  reaches the device. Panel disable and reboot clear sessions. A returned batch
  cannot be retracted. Socket writes have a two-second send timeout.
- The API now implements builtin DELETE as disable and package DELETE as
  uninstall, rejects unknown apps, rejects disabled launches, and rejects
  DELETE on action suffixes. Order requests validate known IDs, unique integer
  slots and duplicate IDs before saving; storage failures can still leave a
  partial NVS update, reported as an error. Queued actions return 202; enable/order changes
  report `restart_required`. Refresh observes asynchronous uninstall completion.
- Optional mDNS advertises `crystal-<12 lowercase chip-MAC hex digits>.local`
  and `_http._tcp` while the panel is enabled. Discovery failure is nonfatal;
  the displayed IP still works. Discovery task/allocations use PSRAM. The
  flash-writing USB worker keeps its internal stack.
- USB headers require a positive bounded decimal length; whitespace, signs,
  overflow and trailing garbage reject. Host timeouts apply to each protocol
  step, allowing a large transfer to take more than 30 seconds overall.
- Catalog downloads verify index signature, package size/hash, package signing
  identity and manifest identity/version. A verified temporary file replaces
  the destination atomically; failed verification preserves an existing file.

The protected HTTP handlers all check `require_token` before reading bodies or
performing actions: app list/upload/order/actions, system/reboot, logs and
session deletion. The page/icons and PIN exchange are intentionally public.
PIN lifetime is ten minutes, the token lifetime is 24 hours, and expiry is
checked using the device monotonic clock. Production-handler simulated-clock
checks avoid a manual 24-hour wait.

## Reviewed v1 UI contract

`components/crystal_app/include/crystal_ui.hpp` is the shared C++ facade.
`DeclarativeApp` configures its factory/style/image hooks using the production
renderer; JSON and callable operations use the same objects and limits.
Every call runs on the LVGL task. Public operation handles are app-owned u32
values, process-unique and never reused. Invalid, deleted, fabricated,
cross-app and wrong-type handles fail. C++ returns zero/false and writes output
arguments only on success; Phase 17's Lua adapter maps failure to nil.
Root deletion/reparenting and reparent cycles reject.

| Plan §6 surface | Implemented mapping |
| --- | --- |
| screen, container/row/col/scroll_view, label/button/image, controls, list/chart, textarea/spinner/dialog/tabview | `screen`, `create(type, parent, options)`; existing JSON node factory and named-widget lookup |
| tab | `tab(view, zero_based_index)`; `tab_labels` creates fixed tabs in order |
| delete | `remove(handle)`, recursively invalidates descendants and series |
| text/value/hidden/enabled/style/focus | `set_*`/`get_*`; text includes textarea, value includes boolean switches |
| scroll/reparent/size | `scroll_to`, `set_parent`, `get_size`; screen coordinates, layout-container parents |
| on/off | click, long_press, change, focus, blur, scroll, delete; one replaceable callback per event |
| rich lists | add/insert/remove/clear/count, icon/primary/secondary/click; optional uniform item height |
| charts | add_series/set_data/set_range/clear; auto Y range until explicitly set; clear retains empty series handles |
| timer/after | app-owned repeating and one-shot timers, explicit cancellation, tracked by resource ledger |
| anim/anim_cancel | opacity, background/text colors and radius; four declared easing curves, delay and completion |

The v1 UI surface above is reviewed and frozen. It does not claim that the
Phase 17 Lua sandbox, async HTTP or script storage implementation exists.
Their interface remains specified in plan §6, with implementation in Phase 17.
Native C++ callers use the facade directly with host factory hooks. Declarative
JSON provides data/bindings and structured actions, rather than arbitrary
callbacks or a scripting language.

Concrete v1 rules and runtime mappings:

- Layout uses bounded flex containers, including row/column wrapping and gaps.
  The plan did not specify grid tracks/placement; no grid API is introduced.
  Widget kinds use `type`; chart variants use `chart_type: line|bar|scatter`.
  JSON tab children and `tab_labels` are equivalent factory inputs.
- Named fonts remain small/medium/large (16/20/28 px), with caption/display
  aliases (14/48 px). The style `font_size` allowlist is 14/16/20/28/48.
  Style updates validate all keys before mutation. Colors are 0xRRGGBB
  integers; opacity is 0–255; invalid style keys/types reject.
- Buttons and list rows have a minimum height of 44 px. Rich list rows fit
  their contents unless `item_height` is set (44–480). Maximum 64 rows/list.
  Images use the existing bounded CR16 decoder under `assets/` or `data/`;
  network URLs and traversal are rejected. Decoded images are cached by path
  for the app lifetime, with 128 KiB aggregate decoded data.
- Widget/timer/animation metadata allocates explicitly from PSRAM on ESP32,
  preserving internal RAM for WiFi/TLS; allocation failure returns zero.
- Maximum 192 live facade objects; one JSON build is limited to 96 nodes and
  depth 8. App metadata is released when widgets are deleted, including button
  maps and action records. Maximum 64 declarative actions and 64 bindings.
- Maximum four chart series/chart, sixteen across an app, and 128 points/series
  (16 points by default). Data uses integers -32768…32766 because LVGL reserves
  32767 for missing points. Scatter X coordinates are point indices.
- Maximum sixteen UI timers and sixteen animations per app. Timer period is
  1…86400000 ms; animation duration 1…60000 ms, delay 0…60000 ms.
  Timers pause/resume; animations cancel on pause, deletion and destroy.
  Cancellation does not call completion. After callbacks retire before running.
- Text updates accept at most 4096 bytes; textarea max_length is 1…4096.
  Focus uses the existing system keyboard and theme. Hiding/disabling/removing
  the field or pausing/destroying its app closes that app's keyboard. An
  independently owned Settings keyboard is left alone. Blur is emitted by
  the existing keyboard teardown. Native setters follow LVGL event semantics;
  setting a value does not synthesize a user change event.
- Dialogs use an app-owned modal backdrop with system colors and 44 px buttons.
  They allow one or two string buttons or `{text, on_click}` buttons and
  dismiss after selection. Callbacks may delete their own widgets safely.
  Declarative `on_click/on_long_press/on_change/on_focus/on_blur/on_scroll/
  on_delete` accept existing timer commands or a structured UI action.

Structured actions use `{call, target, value}`. Supported calls are set_text,
set_value, set_hidden, set_enabled, set_style, set_focus, delete, scroll_to,
set_parent, list_add/insert/remove/clear, chart_add_series/set_data/set_range/
clear, prefixed by `ui.`. `target` is a JSON widget ID; chart `series` and list
`index` are zero-based. `chart_set_range` uses `min/max`; `set_parent` uses
`parent`; scroll uses `animate/align`. Unknown actions fail without evaluating
code. General dynamic creation, getters and callbacks are available to C++
and the future Lua adapter. System-timer actions still require `system_timer`.

Declarative startup uses the plan's Standard 200 ms warning budget. Recorded
95–120 ms Dashboard starts fit this class; 80 ms remains the simple native-app
default. A budget warning remains diagnostic, not an admission failure.

## Validation and remaining limits

Automated evidence is recorded in
[2026-11-13 implementation validation](test-evidence/phase15-16/2026-11-13-implementation-validation.txt).
Real LVGL/Brookesia runs use ASan/UBSan and production keyboard code; they cover
stale/cross-app handles, deletion callbacks, repeated dynamic dialogs/matrices,
rich lists/charts, focus/blur and Settings ownership, timers, animations and
cleanup. The catalog regression uses an ephemeral local HTTPS server and CA,
including rejection before that CA is trusted; it publishes nothing externally.

The retained [A–F](test-evidence/phase15-16/2026-11-13-batch-af-closure.txt),
[G–M](test-evidence/phase15-16/2026-11-13-batch-gm-results.txt) and
[N–R](test-evidence/phase15-16/2026-11-13-batch-nr-results.txt) board evidence
is not repeated. The user completed the [brief core check](APP_PLATFORM_V2_PHASE_15_16_NEXT_STEPS.md)
on 2026-11-14, including keyboard focus and outside-tap dismissal. No USB
board was attached during the original implementation run.

The inherited cold-boot HTTPS failure includes an ESP-TLS connection timeout
followed by successful retries; this alone does not establish OOM. Another
panel+TLS run reached 5807 free internal bytes and a 3328-byte largest block at
different samples, before Dashboard started. Current evidence cannot identify
the timeout's cause or prove simultaneous headroom. Existing bounded timeout/
retry behavior remains; no speculative TLS or flash-worker stack change was
made. Diagnosis and profiling belong to later hardening and do not block
Phase 15/16 development acceptance unless an observed failure prevents a core
flow. These findings are retained, not marked resolved.
