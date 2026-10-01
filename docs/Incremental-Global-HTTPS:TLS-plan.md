# Incremental Global HTTPS/TLS Implementation

**Status:** Active architecture and transport-ownership plan

**Sequencing note (2026-10-01):** Phases 0–2 and the core `crystal_http`
framework are established. The old Phase 3/4 detailed records are historical
and live under `docs/archive/`. Bus-service work is now tracked as Phase 5
slices; [Slice 3 — normalized route metadata](crystal-http-phase5-slice3-normalized-route-metadata-plan.md)
is the next bus slice. Phases 6–8 remain active follow-up work.

Each phase ends with a separate commit and a device test. A phase is accepted only when its visible output matches the expected result.

## How to use this plan

Use this document as the entry point for the remaining work. It defines the
HTTPS/TLS architecture, phase order, transport ownership, migration gates, and
cross-plan sequencing.

Use the companion [bus app integration plan](bus-app-integration-plan.md) for
route, stop, ETA, provider normalization, UI flow, persistence, and LVGL event
contracts. A new session should read both documents whenever a change crosses
the bus-service boundary:

- **Transport-only work:** this plan plus the relevant phase or step document.
- **Bus data or UI work:** the bus app plan plus the relevant phase in this
  plan when the operation performs HTTPS.
- **A new CTB/KMB operation:** both plans, then create a focused step record
  before changing code.

The TLS plan owns how a request is transported and cleaned up. The bus plan
owns what the request means to the application and how normalized data reaches
the user interface. Neither document replaces the other.

## Architecture decision — one HTTPS owner

The target architecture is for every application HTTPS request to use
`crystal_http`. `crystal_http` is the OS-owned boundary for TLS setup,
certificate validation, PSRAM response buffering, timeout and retry policy,
cancellation, response release, and transport diagnostics. Application
components keep URL construction, response parsing, and UI/service events, but
they do not create or clean up `esp_http_client` handles.

This is a deliberate normalization of the current migration. The Phase 3 KMB
catalog and the Phase 4 KMB stop request establish the handoff pattern; the
remaining bus operations and weather requests then move through the same
boundary. The work remains staged because replacing the call site is easy while
preserving ownership and failure behavior is where regressions can occur.

The main failure risks and required controls are:

- **PSRAM exhaustion or fragmentation.** A framework response, an application
  handoff copy, and parsed records can coexist briefly. Every request must have
  an explicit body limit, release the framework response exactly once, and log
  before/after internal heap and PSRAM. A response larger than the bounded
  limit must fail before parsing. If a future endpoint exceeds safe buffering,
  add a streaming framework API as a separate design rather than silently
  increasing limits.
- **Serialized transport contention.** The framework currently has one worker
  and one active HTTPS operation. Weather, catalog, stop, and ETA requests can
  queue behind one another. Each migration must record queue depth and elapsed
  time, cancel obsolete work, and keep interactive requests from inheriting a
  catalog-sized retry budget.
- **Retry and status regressions.** Different endpoints currently have
  different timeout, status, body, and retry assumptions. Each request must
  declare its policy explicitly, preserve HTTP status and transport errors, and
  retry only errors classified as retryable by the framework.
- **Callback lifetime and stale UI data.** A callback can arrive after a
  request timeout, app pause, route change, or network loss. Handoff contexts
  need one owner and one cleanup path; late callbacks may release their own
  response but must not touch destroyed UI state or replace a newer request's
  result.
- **Network-loss retry storms.** Disconnect handling must cancel active owner
  requests and prevent another attempt while there is no IP. Reconnection must
  re-arm the relevant service request once, without duplicating queued work.
- **Migration gaps.** Direct HTTP ownership can return accidentally through a
  helper or a new call site. Phase 7 must enforce a repository check that
  permits `esp_http_client`, `esp_tls`, and certificate-bundle ownership only
  inside `crystal_http`.

## Integration sequencing decision

The HTTPS/TLS migration and bus app integration are related, but they should
advance through a shared service boundary rather than as one combined,
all-at-once phase. The KMB catalog and KMB route-stop work already prove the
`crystal_http` ownership, PSRAM buffering, cancellation, and stale-result
pattern. Their final failure/lifetime acceptance run is deferred so the bus
app can continue, but the implementation remains the baseline for the next
slice.

Use this order for the remaining work:

1. **Freeze the current KMB transport boundary.** Keep the KMB catalog and
   stop requests on `crystal_http`; leave their temporary test switches off.
   Record the final Phase 4 acceptance as deferred rather than reopening the
   already validated implementation.
2. **Do bus-service groundwork.** Define the normalized route, direction,
   stop, and provider-result contracts; move service events onto the LVGL task;
   and make request ids, ownership, cancellation, and error states explicit.
   This work is required whether the provider is KMB or CTB.
3. **Add CTB normalization and stop flow.** Implement the CTB adapter and
   parser against those contracts, then connect the CTB route-stop operation
   to `crystal_http` using the same bounded handoff. Keep provider-specific
   parsing separate until each response has been normalized.
4. **Migrate the remaining bus operations in dependency order.** Route
   variants, CTB stops, and ETA requests can move one operation at a time as
   their normalized contracts become usable. Each operation gets its own
   timeout, body limit, retry, and cancellation policy.
5. **Finish global validation after the channels exist.** Once KMB, CTB, and
   weather all use `crystal_http`, run the deferred Phase 4 matrix together
   with the global ownership and contention checks. This gives the hardening
   work real KMB, CTB, and weather call sites to exercise.

This means the projects are intentionally interleaved at the service boundary:
bus integration supplies the data contracts and user-visible flows, while
HTTPS hardening supplies the transport, memory, retry, and lifetime rules. Do
not merge provider parsers and transport ownership into one change; a failing
gate must still identify whether the problem is normalization or transport.

Global acceptance therefore requires successful normal and failure behavior for
KMB, CTB, and weather, stable heap measurements across repeated requests,
correct cancellation and stale-result handling, and no direct HTTPS owner
outside `crystal_http`. The phases below remain separate so a failure can be
localized to one operation before the next client is migrated.

## Phase 0 — Baseline checkpoint

**Purpose:** Freeze the current working behavior before changing the networking stack.

- Commit the current bus app and TLS-related code.
- Record the current ESP32-S3 firmware size.
- Record free internal heap, largest internal heap block, free PSRAM, and route-stop request timing.
- Capture baseline logs for:
  - Wi-Fi connected
  - KMB route catalog request
  - CTB route catalog request
  - KMB route-stop request
  - TLS timeout or handshake failure

**Visible result:**

```text
TLS baseline:
internal heap: ...
largest internal block: ...
PSRAM free: ...
bus route-stop result: success/failure
```

This phase changes no code.

## Phase 1 — Add the `crystal_http` framework component

**Purpose:** Create the shared transport without migrating existing clients.

Add the new OS-owned component with:

- Request queue.
- One worker task.
- One active HTTPS connection at a time.
- Strict certificate-bundle validation.
- PSRAM response buffering.
- Request ids and owner ids.
- Request-specific timeout, retry, backoff, body limit, and keep-alive options.
- Cancellation by request and owner.
- Response release function.
- Structured transport counters.

No weather or bus code uses it yet.

Add a temporary framework smoke-test request triggered after Wi-Fi obtains an IP. It should request one small known HTTPS JSON endpoint and print the result.

**Visible result:**

```text
crystal_http: worker started
crystal_http: request id=1 owner=framework attempt=1/1
crystal_http: TLS connected
crystal_http: response status=200 body=... bytes
crystal_http: request complete elapsed=... ms
```

Failure output must include the transport error and HTTP status without crashing.

## Phase 2 — Validate TLS memory behavior

Historical procedure: [archived Phase 2 validation](archive/crystal-http-phase2-validation.md)

**Status:** Passed on device. Twenty cold-start HTTPS smoke requests completed successfully with stable cleanup measurements.

**Purpose:** Measure whether PSRAM-backed TLS allocation improves reliability before migrating applications.

Run repeated smoke-test requests while logging:

- Internal heap before TLS.
- Largest internal block before TLS.
- PSRAM free before TLS.
- Internal and PSRAM values during the connection.
- TLS result and elapsed time.
- Peak response size.

Evaluate the existing settings:

- `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`
- TLS record buffer sizes.
- ESP-TLS dynamic buffer support.
- Internal heap reserve.
- PSRAM allocation thresholds.

Only change sdkconfig values when measurements show a need. Do not enlarge buffers blindly.

**Visible result:**

```text
crystal_http: heap before internal=... largest=... psram=...
crystal_http: tls peak internal=... psram=...
crystal_http: success attempts=... elapsed=...
```

Acceptance requires repeated requests without memory leaks or progressive heap loss.

## Phase 3 — Migrate the KMB route catalog

Historical implementation plan: [archived Phase 3 KMB catalog plan](archive/crystal-http-phase3-kmb-catalog.md)

Execute the detailed plan as ten gated steps, Step 0 through Step 9. Each step is a separate reviewable change: freeze the baseline, add the handoff context, submit through `crystal_http`, establish bounded response ownership, reconnect the existing parser and catalog sequence, remove nested retries, remove only the KMB direct-client call, run the failure/lifetime matrix, and record repeatability results. Do not advance when a step's device or build gate fails.

**Purpose:** Move the first real bus operation to the framework.

Change only the KMB route catalog request in `bus_service`.

- Submit the request through `crystal_http`.
- Preserve the existing catalog progress events.
- Preserve PSRAM parsing and filesystem caching.
- Keep CTB on the old path temporarily.
- Remove direct HTTP usage only from the KMB catalog branch.

**Visible result:**

```text
bus_service: KMB request submitted to crystal_http id=...
crystal_http: request id=... attempt=...
crystal_http: response status=200 body=... bytes
bus_service: KMB data downloaded
bus_service: KMB data resolved
bus_service: KMB data saved
```

Test both successful fetch and forced KMB connection failure.

## Phase 4 — Migrate KMB route-stop requests

Historical implementation plan: [archived Phase 4 KMB stops plan](archive/crystal-http-phase4-kmb-stops.md)

Execute the detailed plan as ten gated steps, Step 0 through Step 9. Do not
advance when a build, device, ownership, or lifetime gate fails.

**Status:** Implementation complete for the current slice; final Step 8/9
acceptance is deferred while bus-service normalization and CTB integration
continue. See [archived Phase 4 Step 9 evidence](archive/crystal-http-phase4-step9-repeatability.md).

**Purpose:** Directly address the unstable request currently failing on the ESP32.

Move only KMB route-stop requests to `crystal_http`.

- Keep the corrected URL mapping:
  - `I` → `inbound`
  - `O` → `outbound`
- Use an interactive request policy with the configured timeout and retries.
- Preserve stop-list event delivery and PSRAM ownership.
- Preserve cancellation and stale-request checks.
- Ensure failed responses never free error text as a stop pointer.

**Visible result:**

```text
bus_service: KMB stops submitted route=102 bound=I request=...
crystal_http: request id=... attempt=1/3
crystal_http: TLS failure ... retrying
crystal_http: request id=... attempt=2/3
crystal_http: response status=200 body=... bytes
bus_service: found 34 stops
```

The stop page must show the stop rows after a successful response. Failure must show an error and leave the app usable.

## Phase 5 — Migrate remaining bus HTTPS operations

**Purpose:** Complete the bus service migration in small functional groups.

The completed first implementation slice is documented in
[HTTPS/TLS Phase 5, Slice 1](crystal-http-phase5-step7-slice1-code-guide.md), with its
[signed evidence record](crystal-http-phase5-step7-slice1.md). It combines
the required bus-service contracts with the first CTB normalized operation.
Slice 1 is signed off for handoff; formal device acceptance remains conditional
on the evidence items recorded in its evidence record.

The current bus-service sequence is maintained in the [bus app integration
plan](bus-app-integration-plan.md). The next transport-relevant work is:

1. [Tracked Slice 3: normalized route metadata and destination-first Search](crystal-http-phase5-slice3-normalized-route-metadata-plan.md), using its [code guide](crystal-http-phase5-slice3-normalized-route-metadata-code-guide.md).
2. Continue the remaining bus operations one at a time, preserving the
   provider-neutral contracts and the `crystal_http` handoff.
3. Return to the deferred KMB failure/lifetime matrix after the active bus
   operation slices and weather migration have enough call sites for a useful
   contention test.

After each operation, retain the existing bus event type and UI behavior.

**Visible result for each operation:**

```text
bus_service: operation submitted to crystal_http
crystal_http: request started
crystal_http: response status=...
bus_service: operation parsed successfully
```

No bus source file should directly include or call `esp_http_client` after the
remaining bus HTTPS operations are migrated.

## Phase 6 — Migrate Crystal OS weather networking

**Purpose:** Make weather the second framework client.

Move weather location and forecast requests from `crystal_core`’s private HTTP helper to `crystal_http`.

- Preserve existing weather retry pacing.
- Use a smaller response limit.
- Parse responses in the core service path.
- Continue delivering readings through the existing Crystal OS UI event queue.
- Remove the static weather response buffer and direct HTTP client ownership.

**Visible result:**

```text
crystal_core: weather request submitted to crystal_http
crystal_http: response status=200 body=... bytes
crystal_core: weather parsed successfully
crystal_core: weather event posted
```

Test automatic refresh, manual refresh, Wi-Fi loss, and failed TLS connections.

## Phase 7 — Global ownership enforcement and diagnostics

**Purpose:** Confirm the framework is truly the single HTTPS owner.

- Remove duplicate HTTP helpers and direct TLS includes from weather and bus code.
- Add a repository check that flags `esp_http_client`, `esp_tls`, and certificate-bundle usage outside `crystal_http`.
- Expose framework counters for diagnostics.
- Add logs for queue depth, cancellations, retries, transport failures, HTTP failures, and peak body size.
- Verify owner cancellation when bus app and weather app pause or destroy.
- Verify late callbacks are ignored safely.

**Visible result:**

```text
crystal_http: active=0 queued=0
crystal_http: requests=...
crystal_http: retries=...
crystal_http: successes=...
crystal_http: transport_failures=...
crystal_http: cancellations=...
crystal_http: peak_body=... bytes
```

## Phase 8 — Final reliability validation

Run repeated device tests with separate results recorded for each case:

- Normal KMB route-stop fetch.
- Normal CTB fetch.
- TLS handshake failure.
- TCP connection timeout.
- DNS failure.
- HTTP 422.
- HTTP 5xx.
- Wi-Fi disconnect during connection.
- Wi-Fi disconnect during body download.
- App pause during retry.
- App destroy during an in-flight request.
- Weather and bus requests competing for the transport.
- Repeated fetches checking for heap loss.

Final acceptance requires:

- One shared HTTPS implementation.
- No direct HTTP client ownership outside `crystal_http`.
- No invalid frees or stale UI writes.
- No persistent internal-heap or PSRAM leak.
- Correct cancellation and retry behavior.
- Successful KMB, CTB, and weather migrations.
- Measured TLS memory behavior documented in the repository.
